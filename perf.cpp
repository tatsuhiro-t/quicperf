#define _1GB (1 * 1024 * 1024 * 1024)

static const char *tls_cert;
static const char *tls_key;
static const char *tls_chain;

#include "perf.networking.h"
#include <thread>

#include "perf.services.h"

// mode (client or server) networking (iouring or syscall)
int main (int argc, char *argv[]) 
{
	constexpr uint32_t bytesForTest = _1GB;

	if (strcmp(argv[1], "server") == 0)
	{
		tls_cert = "tls/server.cert.pem";
		tls_key = "tls/server.key.pem";
		tls_chain = "tls/chain.cert.pem";

		globalSetup<Mode::server>();

		auto runServerTest = [&] <Mode mode> (QuicLibrary<mode> *server) -> void {

			server->instanceSetup(443, argc - 3, argv + 3);
			server->startPerfTest();
		};

		if (strcmp(argv[2], "iouring") == 0)
		{
			runServerTest(libraryForChoice<Mode::server | Mode::iouring>());
		}
		else if (strcmp(argv[2], "syscall") == 0)
		{
			runServerTest(libraryForChoice<Mode::server | Mode::syscall>());
		}
	}
	else
	{
		tls_cert = "tls/client.cert.pem";
		tls_key = "tls/client.key.pem";
		tls_chain = "tls/chain.cert.pem";

		globalSetup<Mode::client>();

		// multiple client threads disabled for now

		uint16_t nThreads = 1;
		if (nThreads == 0) nThreads = std::thread::hardware_concurrency();

		std::vector<std::jthread> threads;
		double seconds[nThreads];

		std::atomic<uint16_t> clientsReady = 0;

		struct sockaddr_in6 *server_in6 = (struct sockaddr_in6 *)calloc(1, sizeof(struct sockaddr_in6));
   	server_in6->sin6_family = AF_INET6;
   	server_in6->sin6_flowinfo = 0;
   	server_in6->sin6_port = htons(443);
   	server_in6->sin6_addr = in6addr_loopback;

		auto runClientTest = [=] <Mode mode> (QuicLibrary<mode> *client, uint16_t threadIndex, std::atomic<uint16_t>& clientsReady, double *seconds) -> void {

			client->instanceSetup(111 + threadIndex, argc - 3, argv + 3);

			client->connect((struct sockaddr *)server_in6);
			client->openStream();
			// signal we're ready and wait for other clients
			clientsReady += 1;
			while (clientsReady != nThreads) {}

			// everyone is ready, blast the server

			auto start = std::chrono::high_resolution_clock::now();

			client->startPerfTest(bytesForTest);

			auto end = std::chrono::high_resolution_clock::now();

			double time = (double)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

			printf("%f seconds\n", time / 1000.0f);

			seconds[threadIndex] = time / 1000.0f;
		};	
		
		for (uint16_t threadIndex = 0; threadIndex < nThreads; ++threadIndex)
		{
			threads.emplace_back([&, threadIndex] (std::stop_token st) {
		   	
				if (strcmp(argv[2], "iouring") == 0)
				{
					runClientTest(libraryForChoice<Mode::client | Mode::iouring>(), threadIndex, clientsReady, seconds);
				}
				else if (strcmp(argv[2], "syscall") == 0)
				{
					runClientTest(libraryForChoice<Mode::client | Mode::syscall>(), threadIndex, clientsReady, seconds);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}

		float totalSeconds = 0;

		for (uint16_t threadIndex = 0; threadIndex < nThreads; ++threadIndex)
		{
			totalSeconds += seconds[threadIndex];
		}

		printf("%f Gb/s\n", (nThreads / totalSeconds) * 8.0);
	}
}
