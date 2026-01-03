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
        private byte[] key_;
        private int keySize_;
        public SimpleXORCrypter(byte[] key)
        {
            key_ = key;
            keySize_ = key_.Length;
        }

        public void encrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size)
        {
            for (int i = 0; i < size; i++) {
                dst[i + dstOff] = (byte)(src[i + srcOff]^key_[i%keySize_]);
            }
        }

        public void decrypt(byte[] src, int srcOff, byte[] dst, int dstOff, uint size)
        {
            encrypt(src, srcOff, dst, dstOff, size);
        }
    }
}