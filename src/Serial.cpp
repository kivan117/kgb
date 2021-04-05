#include "Serial.h"
#include <iostream>
#include <iomanip>

Serial::Serial(bool actAsServer, std::string remoteAddr, unsigned port)
	: amServer(actAsServer), remoteHostAddr(remoteAddr), connectionPort(port)
{
	socketset = SDLNet_AllocSocketSet(4);
	if (amServer)
	{
        std::cout << "Setting up server..." << std::endl;

        if (SDLNet_ResolveHost(&localip, NULL, connectionPort) == -1) {
            printf("SDLNet_ResolveHost: %s\n", SDLNet_GetError());
            exit(-117);
        }
        serversock = SDLNet_TCP_Open(&localip);
        if (!serversock) {
            printf("SDLNet_TCP_Open: %s\n", SDLNet_GetError());
            exit(-117);
        }

        if (SDLNet_TCP_AddSocket(socketset, serversock) == -1)
        {
            printf("SDLNet_AddSocket: %s\n", SDLNet_GetError());
            exit(-117);
        }

        std::cout << "Listening for connections on port " << connectionPort << std::endl;

	}
    else
    {

        std::cout << "Connecting to server " << remoteHostAddr << ":" << connectionPort << "..." << std::endl;

        if (SDLNet_ResolveHost(&serverip, remoteHostAddr.c_str(), connectionPort) == -1) {
            printf("SDLNet_ResolveHost: %s\n", SDLNet_GetError());
            exit(-117);
        }

        clientsock = SDLNet_TCP_Open(&serverip);
        if (!clientsock) {
            printf("SDLNet_TCP_Open: %s\n", SDLNet_GetError());
            exit(-117);
        }

        if (SDLNet_TCP_AddSocket(socketset, clientsock) == -1)
        {
            printf("SDLNet_AddSocket: %s\n", SDLNet_GetError());
            exit(-117);
        }
        connected = true;
        std::cout << "Connected." << std::endl;
    }
}

void Serial::Tick()
{
    if (amServer && !connected) //waiting for connection
    {
        int numsocks = SDLNet_CheckSockets(socketset, 0);

        if (numsocks == -1)
        {
            printf("SDLNet_CheckSockets: %s\n", SDLNet_GetError());
            perror("SDLNet_CheckSockets");
            return;
        }
        else if (numsocks > 0)
        {
            if (SDLNet_SocketReady(serversock))
            {
                clientsock = SDLNet_TCP_Accept(serversock);
                if (clientsock)
                {
                    remoteip = SDLNet_TCP_GetPeerAddress(clientsock);
                    if (!remoteip) {
                        printf("SDLNet_TCP_GetPeerAddress: %s\n", SDLNet_GetError());
                        clientsock = NULL;
                        return;
                    }

                    connected = true;
                    if (SDLNet_TCP_AddSocket(socketset, clientsock) == -1)
                    {
                        printf("SDLNet_AddSocket: %s\n", SDLNet_GetError());
                        exit(-117);
                    }

                    SDLNet_TCP_DelSocket(socketset, serversock);
                    SDLNet_TCP_Close(serversock);

                    uint32_t ipaddr = SDL_SwapBE32(remoteip->host);
                    std::cout << "Accepted connection from "
                              << (ipaddr >> 24) << "." << ((ipaddr >> 16) & 0xff) << "." << ((ipaddr >> 8) & 0xff) << (ipaddr & 0xff)
                              << ":" << remoteip->port
                              << std::endl;
                }
            }
        }
    }
    else if (amServer && connected) //connected as host
    {
        int numsocks = SDLNet_CheckSockets(socketset, 0); //check for incoming message, save to queue if available
        if (numsocks > 0)
        {
            if (SDLNet_SocketReady(clientsock))
            {
                //ACCEPT DATA;
                char message[32];
                int len = SDLNet_TCP_Recv(clientsock, message, 32);
                if (!len)
                {
                    printf("SDLNet_TCP_Recv: %s\n", SDLNet_GetError());
                    
                    connected = false;
                    
                    SDLNet_TCP_DelSocket(socketset, clientsock);
                    SDLNet_TCP_Close(clientsock);
                    incomingQueue.push(0xFF);

                    serversock = SDLNet_TCP_Open(&localip);
                    if (!serversock) {
                        printf("SDLNet_TCP_Open: %s\n", SDLNet_GetError());
                        exit(-117);
                    }

                    if (SDLNet_TCP_AddSocket(socketset, serversock) == -1)
                    {
                        printf("SDLNet_AddSocket: %s\n", SDLNet_GetError());
                        exit(-117);
                    }
                }
                else
                {
                    for (int i = 0; i < len; i++)
                    {
                        incomingQueue.push(message[i]);
                        std::cout << "IN:  " << std::hex << std::uppercase << std::setw(2) << ((unsigned)message[i] & 0xFF) << std::endl;
                    }
                }
            }
        }

        //if outgoing message queued, send outgoing message
        if (outgoingQueue.size())
        {
            char outgoingByte = outgoingQueue.front();
            std::cout << "OUT: " << std::hex << std::uppercase << std::setw(2) << ((unsigned)outgoingByte & 0xFF) << std::endl;
            int result = SDLNet_TCP_Send(clientsock, &outgoingByte, 1); /* add 1 for the NULL */
            if (result < 1)
            {
                printf("SDLNet_TCP_Send: %s\n", SDLNet_GetError());
                
                connected = false;

                SDLNet_TCP_DelSocket(socketset, clientsock);
                SDLNet_TCP_Close(clientsock);
                incomingQueue.push(0xFF);

                serversock = SDLNet_TCP_Open(&localip);
                if (!serversock) {
                    printf("SDLNet_TCP_Open: %s\n", SDLNet_GetError());
                    exit(-117);
                }

                if (SDLNet_TCP_AddSocket(socketset, serversock) == -1)
                {
                    printf("SDLNet_AddSocket: %s\n", SDLNet_GetError());
                    exit(-117);
                }
            }
            outgoingQueue.pop();
        }

    }
    else if (connected) //connected as client
    {
        int numsocks = SDLNet_CheckSockets(socketset, 0); //check for incoming message, save to queue if available
        if (numsocks > 0)
        {
            if (SDLNet_SocketReady(clientsock))
            {
                //ACCEPT DATA;
                uint8_t message[32];
                int len = SDLNet_TCP_Recv(clientsock, message, 32);
                if (!len)
                {
                    printf("SDLNet_TCP_Recv: %s\n", SDLNet_GetError());

                    connected = false;

                    SDLNet_TCP_DelSocket(socketset, clientsock);
                    SDLNet_TCP_Close(clientsock);
                    incomingQueue.push(0xFF);
                }
                else
                {
                    for (int i = 0; i < len; i++)
                    {
                        incomingQueue.push(message[i]);
                        std::cout << "IN:  " << std::hex << std::uppercase << std::setw(2) << ((unsigned)message[i] & 0xFF) << std::endl;
                    }
                }
            }
        }

        //if outgoing message queued, send outgoing message
        if (outgoingQueue.size())
        {
            uint8_t outgoingByte = outgoingQueue.front();

            std::cout << "OUT: " << std::hex << std::uppercase << std::setw(2) << ((unsigned)outgoingByte & 0xFF) << std::endl;

            int result = SDLNet_TCP_Send(clientsock, &outgoingByte, 1); /* add 1 for the NULL */
            if (result < 1)
            {
                printf("SDLNet_TCP_Send: %s\n", SDLNet_GetError());

                connected = false;

                SDLNet_TCP_DelSocket(socketset, clientsock);
                SDLNet_TCP_Close(clientsock);
                incomingQueue.push(0xFF);
            }
            outgoingQueue.pop();
        }
    }
}

bool Serial::IsConnected()
{
    return connected;
}
