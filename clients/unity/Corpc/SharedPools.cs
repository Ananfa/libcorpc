using Microsoft.IO;

namespace Corpc
{
    public static class SharedPools
    {
        public static readonly RecyclableMemoryStreamManager Instance =
            new RecyclableMemoryStreamManager(
                blockSize: 128 * 1024,
                largeBufferMultiple: 1024 * 1024,
                maximumBufferSize: 128 * 1024 * 1024);
    }

}