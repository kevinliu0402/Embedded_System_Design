// CSEE 4840 Lab 1: Run and Display Collatz Conjecture Iteration Counts
//
// Spring 2025
//
// By: Kevin Chen
// Uni: kc3521

module lab1( input logic        CLOCK_50,  // 50 MHz Clock input
	     
	     input logic [3:0] 	KEY, // Pushbuttons; KEY[0] is rightmost
	     
	     input logic [9:0] 	SW, // Switches; SW[0] is rightmost
	     
	     // 7-segment LED displays; HEX0 is rightmost
	     output logic [6:0] HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
	     
	     output logic [9:0] LEDR // LEDs above the switches; LED[0] on right
	     );

   logic 			clk, go, done;   
   logic [31:0] 		start;
   /* verilator lint_off UNUSED */
   logic [15:0] 		count;
   /* verilator lint_on UNUSED */
   
   logic [11:0] 		n;          // Value to display (lower 12 bits)
   logic [7:0] 			offset;     // Offset from switch value
   logic [31:0]                 base_n;      // switch-selected start value
   logic [31:0]                 addr_sel;    // address into range RAM
   
   assign clk = CLOCK_50;
   assign base_n = {22'b0, SW};
   assign addr_sel = {24'b0, offset};
   
   // Instantiate the range module with 256 words of RAM
   range #(.RAM_WORDS(256), .RAM_ADDR_BITS(8))
         r ( .clk(clk),
	     .go(go),
	     .start(start),
	     .done(done),
	     .count(count) );
   
   // Button debouncing and edge detection
   logic [3:0] 			key_sync;    // Synchronized button inputs
   logic [3:0] 			key_prev;    // Previous button state
   logic [3:0] 			key_edge;    // Button press edge detection
   
   // Synchronize button inputs (buttons are active-low)
   always_ff @(posedge clk) begin
      key_sync <= ~KEY;  // Invert so 1 = pressed
      key_prev <= key_sync;
      key_edge <= key_sync & ~key_prev;  // Rising edge (button press)
   end
   
   // Counter for button hold timing (~5 Hz when holding)
   // 50 MHz / 5 Hz = 10M cycles. Use a gated counter so each press/hold
   // starts with a fresh delay before auto-repeat kicks in.
   localparam logic [23:0] HOLD_MAX = 24'd10_000_000; // ~0.2s at 50 MHz
   logic [23:0] 		hold_counter;
   logic 			hold_tick;
   
   always_ff @(posedge clk) begin
      if (key_sync[0] || key_sync[1]) begin
	 // Only count while a +/- key is held; reset after each tick.
	 if (hold_tick) begin
	    hold_counter <= 24'd0;
	 end else begin
	    hold_counter <= hold_counter + 1'b1;
	 end
      end else begin
	 // No +/- key held: reset to enforce initial delay on next press
	 hold_counter <= 24'd0;
      end
   end
   
   assign hold_tick = (hold_counter == HOLD_MAX);
   
   // Offset control (KEY[0] = increment, KEY[1] = decrement, KEY[2] = reset)
   always_ff @(posedge clk) begin
      if (key_edge[2]) begin
	 // KEY[2]: Reset offset to 0
	 offset <= 8'd0;
      end else if (key_edge[0] || (key_sync[0] && hold_tick)) begin
	 // KEY[0]: Increment offset
	 offset <= offset + 8'd1;
      end else if (key_edge[1] || (key_sync[1] && hold_tick)) begin
	 // KEY[1]: Decrement offset
	 offset <= offset - 8'd1;
      end
   end
   
   // Start signal (KEY[3] triggers range computation)
   always_ff @(posedge clk) begin
      if (key_edge[3]) begin
	 go <= 1'b1;
      end else begin
	 go <= 1'b0;
      end
   end
   
   // Calculate displayed value and memory address
   assign n = {2'b00, SW} + {4'b0, offset};
   // Use switch value for computation, offset for reading back results
   assign start = go ? base_n : addr_sel;
   
   // Zero the displayed count while a run is in progress to avoid stale data
   /* verilator lint_off UNUSED */
   logic [15:0] display_count;
   /* verilator lint_on UNUSED */
   assign display_count = done ? count : 16'd0;

   // Instantiate hex-to-7-segment decoders for 6 displays
   // Left 3 displays show n (in hex)
   hex7seg h5 (.a(n[11:8]),       .y(HEX5));  // Most significant digit of n
   hex7seg h4 (.a(n[7:4]),        .y(HEX4));
   hex7seg h3 (.a(n[3:0]),        .y(HEX3));  // Least significant digit of n
   
   // Right 3 displays show iteration count (in hex)
   hex7seg h2 (.a(display_count[11:8]), .y(HEX2));  // Most significant digit of count
   hex7seg h1 (.a(display_count[7:4]),  .y(HEX1));
   hex7seg h0 (.a(display_count[3:0]),  .y(HEX0));  // Least significant digit of count
   
   // Show range computation status on LEDs
   assign LEDR[9] = done;      // LED[9] shows when computation is complete
   assign LEDR[8:0] = SW[8:0]; // Display switch values
   
endmodule
