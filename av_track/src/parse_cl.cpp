/******************************************************************************
**
** parse_cl.cpp
**
** Thu Aug  6 19:42:25 2020
** Linux 5.4.0-42-generic (#46-Ubuntu SMP Fri Jul 10 00:24:02 UTC 2020) x86_64
** cerik@Erik-VBox-Ubuntu (Erik Cota-Robles)
**
** Copyright (c) 2020 Erik Cota-Robles
**
** Definition of command line parser class
**
** Automatically created by genparse v0.9.3
**
** See http://genparse.sourceforge.net for details and updates
**
**
******************************************************************************/

#include <stdlib.h>

#if defined(_WIN32) || defined(WIN32)
#include "getopt.h"
#else
#include <getopt.h>
#endif

#include "parse_cl.h"

/*----------------------------------------------------------------------------
**
** Cmdline::Cmdline ()
**
** Constructor method.
**
**--------------------------------------------------------------------------*/

Cmdline::Cmdline(int argc,
                 char *argv[]) // ISO C++17 not allowed: throw (std::string )
{
  extern char *optarg;
  extern int optind;
  int c;

  static struct option long_options[] = {
      {"noStun", no_argument, NULL, 'n'},
      {"udpMux", no_argument, NULL, 'm'},
      {"stunServer", required_argument, NULL, 's'},
      {"stunPort", required_argument, NULL, 't'},
      {"webSocketServer", required_argument, NULL, 'w'},
      {"webSocketPort", required_argument, NULL, 'x'},
      {"turnServer", required_argument, NULL, 'u'},
      {"turnPort", required_argument, NULL, 'p'},
      {"turnUser", required_argument, NULL, 'U'},
      {"turnPass", required_argument, NULL, 'P'},
      {"inputDevice", required_argument, NULL, 'i'},
      {"audioDevice", required_argument, NULL, 'a'}, // Audio device option (microphone)
      {"speakerDevice", required_argument, NULL, 'S'}, // Speaker device option
      {"sampleRate", required_argument, NULL, 'r'},  // Audio sample rate option
      {"channels", required_argument, NULL, 'C'},    // Audio channels option
      {"audioFormat", required_argument, NULL,
       'f'}, // Audio input_format option
      {"videoFormat", required_argument, NULL,
       'V'}, // Video input format option
      {"client_id", required_argument, NULL, 'c'},
      {"debug", no_argument, NULL, 'd'},
      {"resolution", required_argument, NULL, 'R'},
      {"framerate", required_argument, NULL, 'F'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0}};

  _program_name += argv[0];

  /* default values */
  _n = false;
  _m = false;
  _s = "stun.l.google.com";
  _t = 19302;
  _w = "localhost";
  _x = 8000;
  _turnServer = "";
  _turnPort = 3478;
  _turnUser = "";
  _turnPass = "";
#if defined(_WIN32) || defined(WIN32)
  _i = "Integrated Camera"; // Default Windows camera name
#else
  _i = "";
#endif
  _a = "";                // Default audio device (none)
  _speakerDevice = "";    // Default speaker device (none)
  _r = 48000;             // Default sample rate
  _c = 1;                 // Default channels
  _f = "alsa";            // Default audio input_format
  _videoFormat = "mjpeg"; // Default video input format
  _h = false;
  _client_id = "";         // client_id
  _debug = false;          // debug
  _resolution = "640x480"; // resolution
  _framerate = 30;         // framerate

  optind = 0;
  while (
      (c = getopt_long(argc, argv, "a:S:s:t:w:x:u:p:U:R:P:C:i:c:r:f:F:V:denmhv",
                       long_options, &optind)) != -1) {
    switch (c) {
    case 'n':
      _n = true;
      break;

    case 'm':
      _m = true;
      break;

    case 's':
      _s = optarg;
      break;

    case 't':
      _t = atoi(optarg);
      if (_t < 0) {
        std::string err;
        err += "parameter range error: t must be >= 0";
        throw(std::range_error(err));
      }
      if (_t > 65535) {
        std::string err;
        err += "parameter range error: t must be <= 65535";
        throw(std::range_error(err));
      }
      break;

    case 'w':
      _w = optarg;
      break;

    case 'x':
      _x = atoi(optarg);
      if (_x < 0) {
        std::string err;
        err += "parameter range error: x must be >= 0";
        throw(std::range_error(err));
      }
      if (_x > 65535) {
        std::string err;
        err += "parameter range error: x must be <= 65535";
        throw(std::range_error(err));
      }
      break;

    case 'u':
      _turnServer = optarg;
      break;

    case 'p':
      _turnPort = atoi(optarg);
      if (_turnPort < 0) {
        std::string err;
        err += "parameter range error: turn port must be >= 0";
        throw(std::range_error(err));
      }
      if (_turnPort > 65535) {
        std::string err;
        err += "parameter range error: turn port must be <= 65535";
        throw(std::range_error(err));
      }
      break;

    case 'U':
      _turnUser = optarg;
      break;

    case 'P':
      _turnPass = optarg;
      break;

    case 'i':
      _i = optarg;
      break;

    case 'a': // 처리audioDevice 파라미터
      _a = optarg;
      break;

    case 'S': // Speaker device parameter
      _speakerDevice = optarg;
      break;

    case 'r': // 처리sampleRate 파라미터
      _r = atoi(optarg);
      if (_r < 1) {
        std::string err;
        err += "parameter range error: sample rate must be positive";
        throw(std::range_error(err));
      }
      break;

    case 'C':
      _c = atoi(optarg);
      if (_c < 1) {
        std::string err;
        err += "parameter range error: channels must be positive";
        throw(std::range_error(err));
      }
      break;

    case 'f':
      _f = optarg;
      break;

    case 'V':
      _videoFormat = optarg;
      break;

    case 'c':
      _client_id = optarg;
      break;

    case 'R':
      _resolution = optarg;
      break;

    case 'F':
      _framerate = atoi(optarg);
      if (_framerate < 1 || _framerate > 120) {
        std::string err;
        err += "parameter range error: framerate must be between 1 and 120";
        throw(std::range_error(err));
      }
      break;

    case 'd':
      _debug = true;
      break;

    case 'h':
      _h = true;
      this->usage(EXIT_SUCCESS);
      break;

    default:
      this->usage(EXIT_FAILURE);
    }
  } /* while */

  _optind = optind;
}

/*----------------------------------------------------------------------------
**
** Cmdline::usage () and version()
**
** Print out usage (or version) information, then exit.
**
**--------------------------------------------------------------------------*/

void Cmdline::usage(int status) {
  if (status != EXIT_SUCCESS)
    std::cerr << "Try `" << _program_name << " --help' for more information.\n";
  else {
    std::cout << "\
usage: " << _program_name
              << " [ -enstwxhv ] \n\
libdatachannel client implementing WebRTC Data Channels with WebSocket signaling\n\
   [ -n ] [ --noStun ] (type=FLAG)\n\
          Do NOT use a stun server (overrides -s and -t).\n\
   [ -s ] [ --stunServer ] (type=STRING, default=stun.l.google.com)\n\
          STUN server URL or IP address.\n\
   [ -t ] [ --stunPort ] (type=INTEGER, range=0...65535, default=19302)\n\
          STUN server port.\n\
   [ -w ] [ --webSocketServer ] (type=STRING, default=localhost)\n\
          Web socket server URL or IP address.\n\
   [ -x ] [ --webSocketPort ] (type=INTEGER, range=0...65535, default=8000)\n\
          Web socket server port.\n\
   [ -m ] [ --udpMux ] (type=FLAG)\n\
          Use UDP multiplex.\n\
   [ -u ] [ --turnServer ] (type=STRING)\n\
          TURN server URL or IP address.\n\
   [ -p ] [ --turnPort ] (type=INTEGER, range=0...65535, default=3478)\n\
          TURN server port.\n\
   [ -U ] [ --turnUser ] (type=STRING)\n\
          TURN server username.\n\
   [ -P ] [ --turnPass ] (type=STRING)\n\
          TURN server password.\n\
   [ -i ] [ --inputDevice ] (type=STRING, default)=\n\
          Input video device.\n\
   [ -a ] [ --audioDevice ] (type=STRING, default=none)\n\
          Input audio device (microphone).\n\
   [ -S ] [ --speakerDevice ] (type=STRING, default=none)\n\
          Output speaker device.\n\
   [ -r ] [ --sampleRate ] (type=INTEGER, default=48000)\n\
          Audio sample rate.\n\
   [ -C ] [ --channels ] (type=INTEGER, default=1)\n\
          Audio channels.\n\
   [ -f ] [ --audioFormat ] (type=STRING, default=S16LE)\n\
          Audio input_format.\n\
   [ -V ] [ --videoFormat ] (type=STRING, default=mjpeg)\n\
          Video input format.\n\
   [ -c ] [ --client_id ] (type=STRING)\n\
          Client identifier.\n\
   [ -d ] [ --debug ] (type=FLAG)\n\
          Enable debug output.\n\
   [ -R ] [ --resolution ] (type=STRING, default=640x480)\n\
          Video resolution in WIDTHxHEIGHT input_format.\n\
   [ -F ] [ --framerate ] (type=INTEGER, range=1...120, default=30)\n\
          Video encoding framerate.\n\
   [ -h ] [ --help ] (type=FLAG)\n\
          Display this help and exit.\n";
  }
  exit(status);
}