#!/bin/bash

cd "$(dirname "$0")/.."

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

pass_count=0
fail_count=0

echo "Running Jolt compiler tests..."

for file in tests/test_*.jolt; do
    echo -n "Testing $file ... "
    
    ./bin/jolt "$file" > /dev/null 2>&1
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAILED (Compile Error)${NC}"
        fail_count=$((fail_count+1))
        continue
    fi
    
    ./a.out > /dev/null 2>&1
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}PASSED${NC}"
        pass_count=$((pass_count+1))
    else
        echo -e "${RED}FAILED (Execution Error/Assert Failed)${NC}"
        fail_count=$((fail_count+1))
    fi
done

rm -f output.ll a.out

echo ""
echo "Test Summary:"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ $fail_count -gt 0 ]; then
    exit 1
fi

exit 0
