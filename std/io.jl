module "io"

printf :: (s: string) i32

print :: (s: string) i32 {
    return printf(s);
}
