fn main() {
    println!("Hello from the full Rust std on honey-os-2!");

    let mut vec = Vec::new();
    vec.push(1);
    vec.push(2);
    println!("Look, dynamic allocation works: {:?}", vec);
}