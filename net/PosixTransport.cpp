#include "PosixTransport.h"

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

PosixTransport::PosixTransport(const tn6530::Policy &policy)
:	m_fd(-1), m_telnet(policy)
{
	tn6530::Events ev;

	ev.onPayload = [this](const unsigned char *d, int n) {
		if (onHostData) onHostData((const char *)d, n);
	};
	ev.onSend = [this](const unsigned char *d, int n) {
		WriteAll(d, n);
	};
	ev.onEndOfRecord = [this]() {
		if (onEndOfRecord) onEndOfRecord();
	};
	ev.onTrace = [this](const std::string &t) {
		if (onTrace) onTrace(t);
	};

	ev.onLineRead = [this](const tn6530::LineRead &lectura) {
		if (onLineRead) onLineRead(lectura);
	};

	m_telnet.SetEvents(ev);
}

PosixTransport::~PosixTransport()
{
	Close();
}

bool PosixTransport::Connect(const std::string &host, int puerto,
                             std::string *error)
{
	Close();

	char servicio[16];
	std::snprintf(servicio, sizeof(servicio), "%d", puerto);

	struct addrinfo pistas;
	std::memset(&pistas, 0, sizeof(pistas));
	pistas.ai_family = AF_UNSPEC;
	pistas.ai_socktype = SOCK_STREAM;

	struct addrinfo *lista = nullptr;
	int rc = ::getaddrinfo(host.c_str(), servicio, &pistas, &lista);
	if (rc != 0)
	{
		if (error) *error = std::string("no se pudo resolver ") + host + ": " +
		                    ::gai_strerror(rc);
		return false;
	}

	for (struct addrinfo *a = lista; a != nullptr; a = a->ai_next)
	{
		int fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0) continue;
		if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0)
		{
			m_fd = fd;
			break;
		}
		::close(fd);
	}
	::freeaddrinfo(lista);

	if (m_fd < 0)
	{
		if (error) *error = std::string("no se pudo conectar a ") + host + ": " +
		                    std::strerror(errno);
		return false;
	}

	/* Sin Nagle: el 6530 manda respuestas cortas y la latencia se nota. */
	int uno = 1;
	::setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof(uno));

	m_telnet.Start();
	return true;
}

void PosixTransport::Close()
{
	if (m_fd >= 0)
	{
		::close(m_fd);
		m_fd = -1;
	}
}

void PosixTransport::WriteAll(const unsigned char *data, int len)
{
	if (m_fd < 0 || data == nullptr || len <= 0) return;

	if (onRawToHost) onRawToHost((const char *)data, len);

	int escritos = 0;
	while (escritos < len)
	{
		ssize_t n = ::send(m_fd, data + escritos, (size_t)(len - escritos), 0);
		if (n <= 0)
		{
			if (errno == EINTR) continue;
			Close();
			return;
		}
		escritos += (int)n;
	}
}

bool PosixTransport::Poll(int esperaMs)
{
	if (m_fd < 0) return false;

	fd_set lectura;
	FD_ZERO(&lectura);
	FD_SET(m_fd, &lectura);

	struct timeval tv;
	tv.tv_sec  = esperaMs / 1000;
	tv.tv_usec = (esperaMs % 1000) * 1000;

	int rc = ::select(m_fd + 1, &lectura, nullptr, nullptr,
	                  (esperaMs < 0) ? nullptr : &tv);
	if (rc < 0)
	{
		if (errno == EINTR) return true;
		Close();
		return false;
	}
	if (rc == 0) return true;            /* timeout: la conexion sigue viva */

	unsigned char buf[8192];
	ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
	if (n == 0) { Close(); return false; }      /* el host cerro */
	if (n < 0)
	{
		if (errno == EINTR || errno == EAGAIN) return true;
		Close();
		return false;
	}

	if (onRawFromHost) onRawFromHost((const char *)buf, (int)n);
	m_telnet.Feed(buf, (int)n);
	return true;
}

void PosixTransport::SendRaw(const byte *data, int len)
{
	m_telnet.SendPayload((const unsigned char *)data, len);
}
