// src/config.h

#ifndef CONFIG_H
#define CONFIG_H

// IRC IDENTITY
#define NICK "tdeck"
#define REALNAME "ACID DROP Firmware 1.0.0"

// IRC connection
#define SERVER "irc.supernets.org"
#define PORT 6697
#define SSL true

extern const char* user;
extern const char* realname;
extern const char* server;
extern const int port;
extern bool useSSL;
#endif // CONFIG_H
