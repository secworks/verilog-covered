/*
 Name:        generate15.4.v
 Author:      Trevor Williams  (phase1geo@gmail.com)
 Date:        02/19/2009
 Purpose:     Recreates bug 2614516.  The parser emits an error with the '!=' operator in the if statement.
*/

module main;

parameter FOO1 = 4;
parameter FOO2 = 4;

reg  [3:0] a;
reg  [3:0] b;
wire [4:0] c;

generate
   if( FOO1 != FOO2 ) begin : add
       assign c = a + b;
   end else begin
       assign c = a - b;
   end
endgenerate

initial begin
	a = 4'h8;
	b = 4'h4;
        #5;
	b = 4'h3; 
end

initial begin
`ifdef DUMP
        $dumpfile( "generate15.4.vcd" );
        $dumpvars( 0, main );
`endif
        #10;
        $finish;
end

endmodule
