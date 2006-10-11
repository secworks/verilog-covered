module main;

reg a, b;

initial begin
        b = 1'b0;
	wait( a == 1'b1 );
        b = 1'b1;
end

initial begin
	a = 1'b0;
end

initial begin
`ifndef VPI
        $dumpfile( "wait1.vcd" );
        $dumpvars( 0, main );
`endif
        #10;
        $finish;
end

endmodule
