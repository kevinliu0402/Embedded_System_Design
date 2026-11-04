module range
   #(parameter
     RAM_WORDS = 16,            // Number of counts to store in RAM
     RAM_ADDR_BITS = 4)         // Number of RAM address bits
   (input logic         clk,    // Clock
    input logic 	go,     // Read start and start testing
    input logic [31:0] 	start,  // Number to start from or count to read
    output logic 	done,   // True once memory is filled
    output logic [15:0] count); // Iteration count once finished

   logic 		cgo;    // "go" for the Collatz iterator
   logic                cdone;  // "done" from the Collatz iterator
   logic [31:0] 	n;      // number to start the Collatz iterator

// verilator lint_off PINCONNECTEMPTY
   
   // Instantiate the Collatz iterator
   collatz c1(.clk(clk),
	      .go(cgo),
	      .n(n),
	      .done(cdone),
	      .dout());

   logic [RAM_ADDR_BITS - 1:0] 	 num;         // The RAM address to write
   logic 			 running = 0; // True during the iterations
   logic [15:0] 		 din;         // Data to write (iteration count)
   logic 			 we;          // Write enable
   
   // State machine to control the Collatz iterations
   always_ff @(posedge clk) begin
      if (go && !running) begin
	 // Start the range test
	 running <= 1'b1;
	 done <= 1'b0;
	 n <= start;
	 num <= '0;
	 din <= 16'd0;  // Start at 0, will increment to 1
	 cgo <= 1'b1;   // Start Collatz iterator
	 we <= 1'b0;
      end else if (running) begin
	 if (we) begin
	    // Just wrote to RAM, check if we're done
	    we <= 1'b0;
	    if (num == RAM_ADDR_BITS'(RAM_WORDS - 1)) begin
	       // Finished filling RAM
	       running <= 1'b0;
	       done <= 1'b1;
	    end else begin
	       // Move to next number
	       num <= num + 1'b1;
	       n <= n + 32'd1;
	       din <= 16'd0;  // Reset count to 0
	       cgo <= 1'b1;   // Pulse cgo for next iteration
	    end
	 end else if (cgo) begin
	    // Deassert cgo after one cycle (ignore cdone when cgo is high)
	    cgo <= 1'b0;
	 end else if (cdone) begin
	    // Collatz iteration finished, write result to RAM
	    we <= 1'b1;
	 end else begin
	    // Count iterations while Collatz is running
	    din <= din + 16'd1;
	 end
      end
   end

   logic [15:0] 		 mem[RAM_WORDS - 1:0];  // The RAM itself
   logic [RAM_ADDR_BITS - 1:0] 	 addr;                  // Address to read/write

   assign addr = we ? num : start[RAM_ADDR_BITS-1:0];
   
   always_ff @(posedge clk) begin
      if (we) mem[addr] <= din;
      count <= mem[addr];      
   end

endmodule
