module collatz( input logic         clk,   // Clock
		input logic 	    go,    // Load value from n; start iterating
		input logic  [31:0] n,     // Start value; only read when go = 1
		output logic [31:0] dout,  // Iteration value: true after go = 1
		output logic 	    done); // True when dout reaches 1

   always_ff @(posedge clk) begin
      if (go) begin
	 // Load the starting value when go is asserted
	 dout <= n;
	 done <= (n == 32'd1); // Check if we're already at 1
      end else if (!done) begin
	 // Compute next value in the Collatz sequence
	 if (dout == 32'd1) begin
	    done <= 1'b1;
	 end else if (dout[0] == 1'b0) begin
	    // Even: divide by 2
	    dout <= dout >> 1;
	 end else begin
	    // Odd: multiply by 3 and add 1
	    dout <= (dout * 32'd3) + 32'd1;
	 end
      end
   end

endmodule
