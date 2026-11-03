fn main() {
    let (mut best, mut best_start) = (0i64, 0i64);
    for start in 1..=3_000_000i64 {
        let (mut n, mut steps) = (start, 0i64);
        while n != 1 {
            n = if n % 2 == 0 { n / 2 } else { 3 * n + 1 };
            steps += 1;
        }
        if steps > best { best = steps; best_start = start; }
    }
    println!("{} {}", best_start, best);
}
