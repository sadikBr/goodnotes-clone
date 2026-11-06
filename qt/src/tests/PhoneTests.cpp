// Write from your phone: the bits of the protocol that must match the browser's.
#include "../PhonePad.h"
#include "SelfTest.h"

TEST("phone", "the WebSocket handshake answers as browsers expect") {
  // RFC 6455's own example.
  CHECK_EQ(PhonePad::acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}
