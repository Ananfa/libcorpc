using System;

namespace Corpc
{
	public class Constants
	{
		// |body size(4 bytes)|message type(2 bytes)|tag(2 byte)|flag(2 byte)|req serial number(4 bytes)|serial number(4 bytes)|crc(2 bytes)|
		public const int CORPC_MESSAGE_HEAD_SIZE = 20;
		public const int CORPC_MAX_MESSAGE_SIZE = 0x10000;
        public const int CORPC_MAX_UDP_MESSAGE_SIZE = 540;

		public const int CORPC_MESSAGE_FLAG_CRYPT = 0x1;

		public const int CORPC_HEARTBEAT_PERIOD = 5000;
		public const int CORPC_MAX_NO_HEARTBEAT_TIME = 15000;

        public const short CORPC_MSG_TYPE_DISCONNECT = -1;
        public const short CORPC_MSG_TYPE_UDP_UNSHAKE = -110;
        public const short CORPC_MSG_TYPE_UDP_HANDSHAKE_1 = -111;
        public const short CORPC_MSG_TYPE_UDP_HANDSHAKE_2 = -112;
        public const short CORPC_MSG_TYPE_UDP_HANDSHAKE_3 = -113;
        public const short CORPC_MSG_TYPE_UDP_HANDSHAKE_4 = -114;
		public const short CORPC_MSG_TYPE_HEARTBEAT = -115;

	}
}

