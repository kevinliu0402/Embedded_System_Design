# CSEE 4840 Lab 1 - Collatz Conjecture Implementation
## Completion Summary

All four required SystemVerilog modules have been successfully implemented and tested:

### 1. hex7seg.sv - Hexadecimal to 7-Segment Decoder
✓ **Status: Complete and Tested**

Converts 4-bit hexadecimal values (0-F) to 7-segment display patterns.
- Uses active-low encoding (0 = segment on)
- Implemented with case statement for all 16 hex digits
- **Test Result**: All 16 values (0-F) display correctly

### 2. collatz.sv - Collatz Sequence Generator
✓ **Status: Complete and Tested**

Generates Collatz sequences for a given starting value.
- Implements f(n) = n/2 (if even) or 3n+1 (if odd)
- Controlled by 'go' signal to start iteration
- Asserts 'done' when sequence reaches 1
- **Test Result**: Correctly generates sequence for n=7:
  `7 22 11 34 17 52 26 13 40 20 10 5 16 8 4 2 1`

### 3. range.sv - Range Tester with RAM
✓ **Status: Complete and Tested**

Tests Collatz sequences over a range of values and stores iteration counts.
- Uses parameterized RAM (default: 16 words, expandable to 256)
- State machine coordinates Collatz module and RAM writes
- Counts iterations accurately by tracking cycles between go and done
- **Test Result**: All 16 test values match expected counts:
  ```
  n=7: 17 iterations  |  n=15: 18 iterations
  n=8: 4 iterations   |  n=16: 5 iterations
  n=9: 20 iterations  |  n=17: 13 iterations
  n=10: 7 iterations  |  n=18: 21 iterations
  (and so on...)
  ```

### 4. lab1.sv - Top-Level User Interface
✓ **Status: Complete**

Implements full user interface using DE1-SoC hardware:

**Input Controls:**
- SW[9:0]: Set starting value for range test
- KEY[3]: Trigger range computation (leftmost button)
- KEY[2]: Reset offset to 0
- KEY[1]: Decrement displayed value
- KEY[0]: Increment displayed value

**Output Displays:**
- HEX5-HEX3: Display current n value (3 hex digits)
- HEX2-HEX0: Display iteration count (3 hex digits)
- LEDR[9]: Indicates when computation is complete
- LEDR[8:0]: Mirrors switch values

**Features:**
- Button debouncing with edge detection
- Hold-to-repeat functionality (~5 Hz when button held)
- 256-word RAM for storing iteration counts
- Displays any value from n to n+255

### Testing Results

**Verilator Simulation:**
- hex7seg: All 16 hex digits correctly encoded ✓
- collatz: Sequence generation verified for multiple values ✓
- range: Iteration counts match expected values ✓
- lint check: All modules pass with no warnings ✓

### Files for Submission

1. **hex7seg.sv** - 7-segment decoder
2. **collatz.sv** - Collatz sequence generator  
3. **range.sv** - Range tester with RAM
4. **lab1.sv** - Top-level user interface
5. **lab1.tar.gz** - Compressed archive of all .sv files

### Next Steps for FPGA Deployment

To compile and download to the DE1-SoC board:

```bash
# Create Quartus project
make lab1.qpf

# Compile the design
make output_files/lab1.sof

# Download to FPGA (board must be connected via USB)
make program
```

### Demo Checklist

The implementation satisfies all demo requirements:
- ✓ Switch input controls starting value
- ✓ Seven-segment displays show n and iteration count
- ✓ Button press triggers computation
- ✓ Buttons increment/decrement displayed value
- ✓ Button hold provides automatic repeat
- ✓ Reset button returns to base value
- ✓ Visual indication when computation is complete

**All modules implemented, tested, and ready for demonstration!**
