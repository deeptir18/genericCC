#ifndef UDP_SOCKET_HH
#define UDP_SOCKET_HH

#include <string>

#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

class UDPSocket{
public:
	typedef sockaddr_in SockAddress;
private:
	int udp_socket;

	std::string ipaddr;
	int port;
	int srcport;

	bool bound;
public:
	UDPSocket() : udp_socket(-1), ipaddr(), port(), srcport(), bound(false) {
		udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	}

	int bindsocket(std::string ipaddr, int port, int srcport);
	int bindsocket(std::string ipaddr, int port);
	int bindsocket(int port);
	ssize_t senddata(const char* data, ssize_t size, SockAddress *s_dest_addr);
	ssize_t senddata(const char* data, ssize_t size, std::string dest_ip, int dest_port);
	int receivedata(char* buffer, int bufsize, int timeout, SockAddress &other_addr);
  bool set_reuse() {
    int enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
      return -1;
    return 0;
  }
  bool close_socket() {
    close(udp_socket);
    return 0;
  }
	static void decipher_socket_addr(SockAddress addr, std::string& ip_addr, int& port);
	static std::string decipher_socket_addr(SockAddress addr);
};

#endif
