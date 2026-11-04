module hex7seg(input logic  [3:0] a,
	       output logic [6:0] y);

   // Seven-segment display decoder for hexadecimal digits
   // Segments are active-low: 0 = on, 1 = off
   // Bit ordering: {g, f, e, d, c, b, a}
   
   always_comb begin
      case(a)
	4'h0: y = 7'b1000000; // 0x40
	4'h1: y = 7'b1111001; // 0x79
	4'h2: y = 7'b0100100; // 0x24
	4'h3: y = 7'b0110000; // 0x30
	4'h4: y = 7'b0011001; // 0x19
	4'h5: y = 7'b0010010; // 0x12
	4'h6: y = 7'b0000010; // 0x02
	4'h7: y = 7'b1111000; // 0x78
	4'h8: y = 7'b0000000; // 0x00
	4'h9: y = 7'b0010000; // 0x10
	4'ha: y = 7'b0001000; // 0x08
	4'hb: y = 7'b0000011; // 0x03
	4'hc: y = 7'b1000110; // 0x46
	4'hd: y = 7'b0100001; // 0x21
	4'he: y = 7'b0000110; // 0x06
	4'hf: y = 7'b0001110; // 0x0e
	default: y = 7'b1111111; // All off
      endcase
   end
   
endmodule
