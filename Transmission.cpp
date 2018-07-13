#include "Transmission.h"
#include <iostream>

char* Transmission::getHostIP()
{
	char hostName[256];
	gethostname(hostName, sizeof(hostName));
	HOSTENT* host = gethostbyname(hostName);
	char* hostIP;
	strcpy(hostIP, inet_ntoa(*(in_addr*)*host->h_addr_list));
	return hostIP;
}

void Transmission::sendData(char* data, int tot)
{
	int offset = 0;
	while (offset != tot) {
		int len = min(BUFF_SIZE, tot - offset);
		send(sock, data + offset, len, 0);
		offset += len;
	}
}

void Transmission::recvData(char* data, int tot)
{
	int len = 0;
	int offset = 0;
	while (len = recv(sock, data + offset, min(BUFF_SIZE, tot - offset), 0)) {
		offset += len;
		if (offset == tot) {
			break;
		}
	}
}

Transmission::Transmission(bool isServer)
{
	memset(&sockAddr, 0, sizeof(sockAddr));
	sockAddr.sin_family = PF_INET;
	sockAddr.sin_addr.s_addr = inet_addr(IP);
	sockAddr.sin_port = htons(port);

	if (isServer) {
		//-- Server
		SOCKET sockSrv = socket(PF_INET, SOCK_STREAM, 0);
		bind(sockSrv, (SOCKADDR*)&sockAddr, sizeof(SOCKADDR));
		listen(sockSrv, 20);
		int len = sizeof(SOCKADDR);
		sock = accept(sockSrv, (SOCKADDR*)&sockAddr, &len);
	}
	else {
		//-- Client
		sock = socket(AF_INET, SOCK_STREAM, 0);
		connect(sock, (SOCKADDR*)&sockAddr, sizeof(sockAddr));
	}
}

Transmission::Transmission()
{
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	bool isServer = (strcmp(getHostIP(), IP) == 0);
	this->Transmission::Transmission(isServer);
}

Transmission::~Transmission()
{
	closesocket(sock);
	WSACleanup();
}

void Transmission::sendRGBD(UINT16* sendDepth, RGBQUAD* sendColor)
{
	sendData((char*)sendDepth, LEN * sizeof(UINT16));
	sendData((char*)sendColor, LEN * sizeof(RGBQUAD));
}


void Transmission::recvRGBD(UINT16*& recvDepth, RGBQUAD*& recvColor)
{
	recvData((char*)recvDepth, LEN * sizeof(UINT16));
	recvData((char*)recvColor, LEN * sizeof(RGBQUAD));
}

void Transmission::sendInt(int data)
{
	sendData((char*)(&data), sizeof(int));
}

void Transmission::receiveInt(int& data)
{
	receiveData((char*)(&data), sizeof(int));
}

void Transmission::sendFloat(float data) {
	sendData((char*)(&data), sizeof(float));
}

void Transmission::receiveFloat(float& data) {
	receiveData((char*)(&data), sizeof(float));
}

void Transmission::sendArray(char* data, size_t length) {
	sendData(data, length);
}

void Transmission::receiveArray(char*& data, size_t length) {
	recvData(data, length);
}
