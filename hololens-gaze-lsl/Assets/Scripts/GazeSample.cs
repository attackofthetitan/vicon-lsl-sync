namespace GazeLSL
{
    public struct GazeSample
    {
        // Capture timestamp in the LSL local_clock domain. This is metadata
        // carried alongside the fixed channel payload and is not an LSL channel.
        public double Timestamp;

        public double CombinedOriginX;
        public double CombinedOriginY;
        public double CombinedOriginZ;
        public double CombinedDirectionX;
        public double CombinedDirectionY;
        public double CombinedDirectionZ;
        public bool CombinedValid;

        public double LeftEyeOriginX;
        public double LeftEyeOriginY;
        public double LeftEyeOriginZ;
        public double LeftEyeDirectionX;
        public double LeftEyeDirectionY;
        public double LeftEyeDirectionZ;
        public bool LeftEyeValid;

        public double RightEyeOriginX;
        public double RightEyeOriginY;
        public double RightEyeOriginZ;
        public double RightEyeDirectionX;
        public double RightEyeDirectionY;
        public double RightEyeDirectionZ;
        public bool RightEyeValid;
    }
}
