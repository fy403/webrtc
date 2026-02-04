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

class Cmdline {
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
  std::string _i;
  std::string _a;             // Audio device parameter (microphone)
  std::string _speakerDevice; // Speaker device parameter
  int _r;                     // Audio sample rate
  int _c;                     // Audio channels
  std::string _f;             // Audio input_format
  std::string _videoFormat;   // Video input format
  std::string _videoCodec;    // Video codec (h264 or h265)
  bool _h;
  std::string _client_id;  // 新添加的client_id参数
  bool _debug;             // 新添加的debug参数
  std::string _resolution; // 新添加的resolution参数
  int _framerate;          // 新添加的framerate参数

  // Audio output parameters
  int _out_sample_rate; // Audio output sample rate
  int _out_channels;    // Audio output channels
  float _volume;        // Audio volume control

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
  std::string audioDevice() const {
    return _a;
  } // Audio device getter (microphone)
  std::string speakerDevice() const {
    return _speakerDevice;
  }                                              // Speaker device getter
  int sampleRate() const { return _r; }          // Audio sample rate getter
  int channels() const { return _c; }            // Audio channels getter
  std::string audioFormat() const { return _f; } // Audio input_format getter
  std::string videoFormat() const {
    return _videoFormat;
  } // Video input format getter
  std::string videoCodec() const {
    return _videoCodec;
  } // Video codec getter (h264 or h265)
  bool h() const { return _h; }
  std::string clientId() const {
    return _client_id;
  } // 新添加的client_id getter
  bool debug() const { return _debug; } // 新添加的debug getter
  std::string resolution() const {
    return _resolution;
  } // 新添加的resolution getter
  int framerate() const { return _framerate; } // 新添加的framerate getter

  // Audio output parameter getters
  int outSampleRate() const { return _out_sample_rate; }
  int outChannels() const { return _out_channels; }
  float volume() const { return _volume; }
};

#endif