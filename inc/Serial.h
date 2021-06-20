#pragma once
#include <SDL_net.h>
#include <string>
#include <queue>
class Serial
{
public:
	Serial(bool actAsServer, std::string remoteAddr, unsigned port);
	void Tick();
	bool IsConnected();
	uint8_t SB{ 0xFF };
	bool expectingResponse{ false };
	std::queue<uint8_t> incomingQueue, outgoingQueue;
private:
	bool amServer;
	std::string remoteHostAddr;
	unsigned connectionPort;

	bool connected{ false };

	SDLNet_SocketSet socketset;
	TCPsocket serversock, clientsock;
	IPaddress localip, serverip;

	IPaddress* remoteip;


};

