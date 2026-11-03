fn main() {
    let mut nums: Vec<i64> = Vec::new();
    for i in 1..=30_000_000i64 { nums.push(i % 1000); }
    let mut total: i64 = 0;
    for n in &nums { total += n; }
    println!("{}", total);
}
