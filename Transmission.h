#ifndef TRANSMISSION_H
#define TRANSMISSION_H

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <Windows.h>
#define BUFF_SIZE 1024
#define LEN 424 * 512

class Transmission {
private:
	const char* IP = "192.168.1.1";
	const int port = 1234;

	WSADATA wsaData;
	sockaddr_in sockAddr;
	SOCKET sock;

	char* getHostIP();
	void sendData(char* data, int tot);
	void recvData(char* data, int tot);

public:
	Transmission(bool isServer);
	Transmission();
	~Transmission();
	void sendRGBD(UINT16* sendDepth, RGBQUAD* sendCoor);
	void recvRGBD(UINT16*& recvDepth, RGBQUAD*& recvColor);
	void sendInt(int data);
	void receiveInt(int& data);
	void sendFloat(float data);
	void receiveFloat(float& data);
	void sendArray(char* data, size_t length);
	void receiveArray(char*& data, size_t length);
};

#endif
