using System;

internal static partial class Program
{
    private static void True(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }

    private static void False(bool value, string message) => True(!value, message);

    private static void Equal(double expected, double actual)
    {
        if (expected != actual) throw new InvalidOperationException($"Expected {expected}, got {actual}.");
    }

    private static void Equal(long expected, long actual)
    {
        if (expected != actual) throw new InvalidOperationException($"Expected {expected}, got {actual}.");
    }

    private static void Equal(string expected, string actual)
    {
        if (!String.Equals(expected, actual, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Expected {expected}, got {actual}.");
        }
    }

    private static void Same(object expected, object actual)
    {
        if (!ReferenceEquals(expected, actual)) throw new InvalidOperationException("Objects are not identical.");
    }

    private static void Near(double expected, double actual)
    {
        if (Math.Abs(expected - actual) > 0.000001)
        {
            throw new InvalidOperationException($"Expected {expected}, got {actual}.");
        }
    }
}
