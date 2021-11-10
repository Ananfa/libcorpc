using System;

namespace Corpc
{   
    public interface ICrypter
    {
        void encrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size);
        void decrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size);
    }

    public class SimpleXORCrypter: ICrypter
    {
        private byte[] _key;
        private int _keySize;
        public SimpleXORCrypter(byte[] key)
        {
            _key = key;
            _keySize = _key.Length;
        }

        public void encrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size)
        {
            for (int i = 0; i < size; i++) {
                dst[i + dstOff] = (byte)(src[i + srcOff]^_key[i%_keySize]);
            }
        }

        public void decrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size)
        {
            encrypt(src, srcOff, dst, dstOff, size);
        }
    }
}