using System;

namespace Corpc
{   
    public interface ICrypter
    {
        void encrypt(byte[] src, byte[] dst, uint size);
        void decrypt(byte[] src, byte[] dst, uint size);
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

        public void encrypt(byte[] src, byte[] dst, uint size)
        {
            for (int i = 0; i < size; i++) {
                dst[i] = (byte)(src[i]^_key[i%_keySize]);
            }
        }

        public void decrypt(byte[] src, byte[] dst, uint size)
        {
            encrypt(src, dst, size);
        }
    }
}