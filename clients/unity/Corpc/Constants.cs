using System;

namespace Corpc
{
	public class Constants
	{
		public const int CORPC_MESSAGE_HEAD_SIZE = 8;
		public const int CORPC_MAX_MESSAGE_SIZE = 0x10000;
        public const int CORPC_MAX_UDP_MESSAGE_SIZE = 540;

		public const int CORPC_HEARTBEAT_PERIOD = 5000;
		public const int CORPC_MAX_NO_HEARTBEAT_TIME = 15000;

        public const int CORPC_MSG_TYPE_DISCONNECT = -1;
        public const int CORPC_MSG_TYPE_UDP_UNSHAKE = -110;
        public const int CORPC_MSG_TYPE_UDP_HANDSHAKE_1 = -111;
        public const int CORPC_MSG_TYPE_UDP_HANDSHAKE_2 = -112;
        public const int CORPC_MSG_TYPE_UDP_HANDSHAKE_3 = -113;
		public const int CORPC_MSG_TYPE_HEARTBEAT = -115;

	}
}

