#pragma once

#include <cstddef>
#include <string>

#include <enet/enet.h>

#include <util/tick_clock.hpp>
#include <net/port.hpp>
#include <net/server_data.hpp>

class Client
{
public:
    Client(std::string server_hostname, net::Port port, double tick_rate);
    ~Client();
private:
    static const std::size_t    CONNECT_TIMEOUT = 5000;
    static const std::size_t DISCONNECT_TIMEOUT = 5000;

    void connect_to(ENetAddress *server_addr);
    void disconnect();
    void run();
    void tick();
    void handle_input();
    void handle_output();
    void receive(ENetPacket *packet); //TODO move those to a different class?
    ENetPacket *send();               //

    ENetHost       *enet_client;
    ENetPeer       *enet_server;
    util::TickClock tick_clock;
    net::ServerData server_data;
};
