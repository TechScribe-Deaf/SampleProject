using System;
using System.Runtime.InteropServices;

namespace Sample {
    [StructLayout(LayoutKind.Sequential)]
    public struct PricePoint {
        public float High {get;set;}
        public float Low {get;set;}
        public float Open {get;set;}
        public float Close {get;set;}
    }
}