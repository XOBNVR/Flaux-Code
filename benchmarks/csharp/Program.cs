using System;
using System.Collections.Generic;

static class Bench
{
    static long Fib(long n) => n < 2 ? n : Fib(n - 1) + Fib(n - 2);

    static void Sieve()
    {
        const long limit = 50_000_000;
        var isPrime = new bool[limit + 1];
        Array.Fill(isPrime, true);
        isPrime[0] = isPrime[1] = false;
        long count = 0;
        for (long i = 2; i <= limit; i++)
        {
            if (isPrime[i])
            {
                count++;
                for (long j = i * i; j <= limit; j += i) isPrime[j] = false;
            }
        }
        Console.WriteLine(count);
    }

    static void Collatz()
    {
        long best = 0, bestStart = 0;
        for (long start = 1; start <= 3_000_000; start++)
        {
            long n = start, steps = 0;
            while (n != 1)
            {
                if (n % 2 == 0) n = n / 2; else n = 3 * n + 1;
                steps++;
            }
            if (steps > best) { best = steps; bestStart = start; }
        }
        Console.WriteLine($"{bestStart} {best}");
    }

    static void Mandelbrot()
    {
        const long size = 1200, maxIter = 200;
        long total = 0;
        for (long py = 0; py < size; py++)
        {
            double y0 = (double)py / size * 2.0 - 1.0;
            for (long px = 0; px < size; px++)
            {
                double x0 = (double)px / size * 3.0 - 2.0;
                double x = 0, y = 0;
                long i = 0;
                while (i < maxIter && x * x + y * y <= 4.0)
                {
                    double xt = x * x - y * y + x0;
                    y = 2.0 * x * y + y0;
                    x = xt;
                    i++;
                }
                total += i;
            }
        }
        Console.WriteLine(total);
    }

    static void Lists()
    {
        var nums = new List<long>();
        for (long i = 1; i <= 30_000_000; i++) nums.Add(i % 1000);
        long total = 0;
        foreach (var n in nums) total += n;
        Console.WriteLine(total);
    }

    static void Main(string[] args)
    {
        switch (args[0])
        {
            case "fib": Console.WriteLine(Fib(40)); break;
            case "sieve": Sieve(); break;
            case "collatz": Collatz(); break;
            case "mandelbrot": Mandelbrot(); break;
            case "lists": Lists(); break;
        }
    }
}
