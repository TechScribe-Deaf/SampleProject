using System;
using System.Linq;
using System.Runtime.InteropServices;
namespace Sample;
public static unsafe class LibComputeSample {
    /// <summary>
    /// Compute a given workitem and returns Smooth Moving Average result.
    /// </summary>
    public static float[]? Compute(WorkItem item)
    {
        var output = new Indicator[item.PricePoints.Length];
        fixed (Indicator* outPtr = output)
        fixed (PricePoint* ptr = item.PricePoints)
        {
            if (ComputeResult((Candlestick*)ptr, (nuint)item.PricePoints.Length, outPtr) != 0)
                return null; // Error occurs
            return output.Select(I => I.sma).ToArray(); // Select only SMA for this.
        }
    }

    [DllImport("computesample")]
    private static extern int ComputeResult(Candlestick* kline, nuint kline_elements_count, Indicator* output);

    [StructLayout(LayoutKind.Sequential)]
    private struct Candlestick
    {
        public float open;
        public float high;
        public float low;
        public float close;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Indicator
    {
        public float sma;
        public float padding1; // Can be used for future Indicators inclusion like DI+/DI-/ADX and so forth
        public float padding2;
        public float padding3;
    }
}