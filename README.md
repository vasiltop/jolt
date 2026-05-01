# jolt

Jolt is a statically-typed, natively compiled programming language.

## Installation

```bash
git clone https://github.com/vasiltop/jolt
cd jolt
make
```

## Usage

```bash
./bin/jolt <PATH>
./a.out
```

## Examples

Code examples can be found in the `examples/` directory.

## Variables

```rust
let message = "Hello, world!";
let n: i32 = 42;
```

## Control Flow

```rust
if x == 1 && true {
    x = 4;
} else {
    x = 7;
}

let i: i32 = 0;
while i < 10 {
    i = i + 1;
}
```

## Functions

```rust
fn add(a: i32, b: i32) i32 {
    ret a + b;
}

fn main() i32 {
    let added: i32 = add(4, 2);
    ret 0;
}
```

## Arrays

```rust
fn main() i32 {
    let a = [2, 4];

    ret 0;
}
```

## Structs

```rust
import "./std/io.jolt";

struct Other {
    b: [i32; 2];
}

struct Test {
    b: Other;
    other: Other;
}

fn main() i32 {
    let a = Test { 
        b: Other { b: [1, 2] },
        other: Other { b: [3, 4] }
    };

    io:printf("%d\n", a.b.b[1]);
    io:printf("%d\n", a.other.b[1]);
    
    ret 0;
}
```
