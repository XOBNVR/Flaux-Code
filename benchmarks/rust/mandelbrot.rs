fn main() {
    let (size, max_iter) = (1200i64, 200i64);
    let mut total: i64 = 0;
    for py in 0..size {
        let y0 = py as f64 / size as f64 * 2.0 - 1.0;
        for px in 0..size {
            let x0 = px as f64 / size as f64 * 3.0 - 2.0;
            let (mut x, mut y, mut i) = (0.0f64, 0.0f64, 0i64);
            while i < max_iter && x * x + y * y <= 4.0 {
                let xt = x * x - y * y + x0;
                y = 2.0 * x * y + y0;
                x = xt;
                i += 1;
            }
            total += i;
        }
    }
    println!("{}", total);
}
