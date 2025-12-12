/******************************************************************************
**
** parse_cl.h
**
** Thu Aug  6 19:42:25 2020
** Linux 5.4.0-42-generic (#46-Ubuntu SMP Fri Jul 10 00:24:02 UTC 2020) x86_64
** cerik@Erik-VBox-Ubuntu (Erik Cota-Robles)
**
** Copyright (c) 2020 Erik Cota-Robles
**
** Header file for command line parser class
**
** Automatically created by genparse v0.9.3
**
** See http://genparse.sourceforge.net for details and updates
**
******************************************************************************/

#ifndef CMDLINE_H
#define CMDLINE_H

#include <iostream>
#include <string>

/*----------------------------------------------------------------------------
**
** class Cmdline
**
** command line parser class
**
**--------------------------------------------------------------------------*/

class Cmdline
{
private:
  /* parameters */
  bool _n;
  bool _m;
  std::string _s;
  int _t;
  std::string _w;
  int _x;
  std::string _turnServer;
  int _turnPort;
  std::string _turnUser;
  std::string _turnPass;
  std::string _usbPort;
  std::string _i;
  bool _h;
  std::string _client_id; // 新添加的client_id参数
  bool _debug;            // 新添加的debug参数
  int _port;              // 新添加的port参数
  std::string _device;    // 新添加的device参数
  std::string _ttyPort;   // 新添加的ttyPort参数
  int _ttyBaudrate;       // 新添加的ttyBaudrate参数
  std::string _gsmPort;   // 新添加的gsmPort参数
  int _gsmBaudrate;       // 新添加的gsmBaudrate参数
  std::string _motorDriverType; // 新添加的motorDriverType参数

  /* other stuff to keep track of */
  std::string _program_name;
  int _optind;

public:
  /* constructor and destructor */
  Cmdline(int, char **); // ISO C++17 not allowed: throw (std::string);
  ~Cmdline() {}

  /* usage function */
  void usage(int status);

  /* return next (non-option) parameter */
  int next_param() { return _optind; }

  bool noStun() const { return _n; }
  bool udpMux() const { return _m; }
  std::string stunServer() const { return _s; }
  int stunPort() const { return _t; }
  std::string webSocketServer() const { return _w; }
  int webSocketPort() const { return _x; }
  std::string turnServer() const { return _turnServer; }
  int turnPort() const { return _turnPort; }
  std::string turnUser() const { return _turnUser; }
  std::string turnPass() const { return _turnPass; }
  std::string inputDevice() const { return _i; }
  std::string usbDevice() const { return _usbPort; }
  bool h() const { return _h; }
  std::string clientId() const { return _client_id; } // 新添加的client_id getter
  bool debug() const { return _debug; }               // 新添加的debug getter
  std::string ttyPort() const { return _ttyPort; }    // 新添加的ttyPort getter
  int ttyBaudrate() const { return _ttyBaudrate; }    // 新添加的ttyBaudrate getter
  std::string gsmPort() const { return _gsmPort; }    // 新添加的gsmPort getter
  int gsmBaudrate() const { return _gsmBaudrate; }    // 新添加的gsmBaudrate getter
  std::string motorDriverType() const { return _motorDriverType; } // 新添加的motorDriverType getter
};

#endif