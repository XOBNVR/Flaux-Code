fn main() {
    let limit: usize = 50_000_000;
    let mut is_prime = vec![true; limit + 1];
    is_prime[0] = false;
    is_prime[1] = false;
    let mut count: i64 = 0;
    for i in 2..=limit {
        if is_prime[i] {
            count += 1;
            let mut j = i * i;
            while j <= limit {
                is_prime[j] = false;
                j += i;
            }
        }
    }
    println!("{}", count);
}
