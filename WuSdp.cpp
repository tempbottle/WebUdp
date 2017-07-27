#include "WuSdp.h"
#include <stdio.h>
#include <string.h>
#include "WuArena.h"
#include "WuRng.h"

enum SdpParseState { kParseIgnore, kParseType, kParseEq, kParseField };

bool BeginsWith(const char* s, size_t len, const char* prefix, size_t plen) {
  if (plen > len) return false;

  for (size_t i = 0; i < plen; i++) {
    char a = s[i];
    char b = prefix[i];

    if (a != b) return false;
  }

  return true;
}

bool GetIceValue(const char* field, size_t len, const char* name, IceField* o) {
  if (BeginsWith(field, len, name, strlen(name))) {
    for (size_t i = 0; i < len; i++) {
      char c = field[i];
      if (c == ':') {
        size_t valueBegin = i + 1;
        if (valueBegin < len) {
          size_t valueLength = len - valueBegin;
          o->value = field + valueBegin;
          o->length = int32_t(valueLength);
          return true;
        }
        break;
      }
    }
  }

  return false;
}

void ParseSdpField(const char* field, size_t len, ICESdpFields* fields) {
  GetIceValue(field, len, "ice-ufrag", &fields->ufrag);
  GetIceValue(field, len, "ice-pwd", &fields->password);
  GetIceValue(field, len, "mid", &fields->mid);
}

bool ParseSdp(const char* sdp, size_t len, ICESdpFields* fields) {
  memset(fields, 0, sizeof(ICESdpFields));

  SdpParseState state = kParseType;
  size_t begin = 0;
  size_t length = 0;

  for (size_t i = 0; i < len; i++) {
    char c = sdp[i];
    switch (state) {
      case kParseType: {
        if (c == 'a') {
          state = kParseEq;
        } else {
          state = kParseIgnore;
        }
        break;
      }
      case kParseEq: {
        if (c == '=') {
          state = kParseField;
          begin = i + 1;
          length = 0;
          break;
        } else {
          return false;
        }
      }
      case kParseField: {
        switch (c) {
          case '\n': {
            ParseSdpField(sdp + begin, length, fields);
            length = 0;
            state = kParseType;
            break;
          }
          case '\r': {
            state = kParseIgnore;
            ParseSdpField(sdp + begin, length, fields);
            length = 0;
            break;
          };
          default: { length++; }
        }
      }
      default: {
        if (c == '\n') state = kParseType;
      }
    }
  }

  return true;
}

const char* GenerateSDP(WuArena* arena, const char* certFingerprint,
                        const char* serverIp, const char* serverPort,
                        const char* ufrag, int32_t ufragLen, const char* pass,
                        int32_t passLen, const ICESdpFields* remote,
                        int* outLength) {
  /* NOTE: The newer version should contain these fields:
   * m=application {port} DTLS/SCTP webrtc-datachannel
   * a=sctp-port:{port}
   * instead of
   * m=application {port} DTLS/SCTP {port}
   * a=sctpmap:{port} webrtc-datachannel {max-size}
   */
  char buf[3096];
  snprintf(buf, sizeof(buf),
           "{\"answer\":{\"sdp\":\"v=0\\r\\n"
           "o=- %u 1 IN IP4 %s\\r\\n"
           "s=-\\r\\n"
           "t=0 0\\r\\n"
           "m=application %s DTLS/SCTP %s\\r\\n"
           "c=IN IP4 %s\\r\\n"
           "a=ice-lite\\r\\n"
           "a=ice-ufrag:%.*s\\r\\n"
           "a=ice-pwd:%.*s\\r\\n"
           "a=fingerprint:sha-256 %s\\r\\n"
           "a=ice-options:trickle\\r\\n"
           "a=setup:passive\\r\\n"
           "a=mid:%.*s\\r\\n"
           "a=sctpmap:%s webrtc-datachannel 1024\\r\\n\","
           "\"type\":\"answer\"},\"candidate\":{\"sdpMLineIndex\":0,"
           "\"sdpMid\":\"%.*s\",\"candidate\":\"candidate:1 1 UDP %u %s %s typ "
           "host\"}}",
           WuRandomU32(), serverPort, serverIp, serverPort, serverIp, ufragLen,
           ufrag, passLen, pass, certFingerprint, remote->mid.length,
           remote->mid.value, serverPort, remote->mid.length, remote->mid.value,
           WuRandomU32(), serverIp, serverPort);

  int32_t length = strlen(buf);
  char* sdp = (char*)WuArenaAcquire(arena, length);
  memcpy(sdp, buf, length);
  *outLength = length;

  return sdp;
}
