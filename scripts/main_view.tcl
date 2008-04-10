#!/usr/bin/env wish

# Include the necessary auxiliary files 
source [file join $HOME scripts menu_create.tcl]
source [file join $HOME scripts cov_create.tcl]
source [file join $HOME scripts process_file.tcl]
source [file join $HOME scripts toggle.tcl]
source [file join $HOME scripts comb.tcl]
source [file join $HOME scripts fsm.tcl]
source [file join $HOME scripts help_wrapper.tcl]
source [file join $HOME scripts help.tcl]
source [file join $HOME scripts summary.tcl]
source [file join $HOME scripts preferences.tcl]
source [file join $HOME scripts cdd_view.tcl]
source [file join $HOME scripts assert.tcl]
source [file join $HOME scripts verilog.tcl]
source [file join $HOME scripts memory.tcl]

set last_lb_index      ""
set lwidth             -1 
set lwidth_h1          -1
set start_search_index 1.0
set curr_uncov_index   ""
set prev_uncov_index   ""
set next_uncov_index   ""

proc main_view {} {

  global race_msgs prev_uncov_index next_uncov_index

  # Start off 

  # Get all race condition reason messages
  set race_msgs ""
  tcl_func_get_race_reason_msgs

  # Create the frame for menubar creation
  menu_create

  # Create the information frame
  frame .covbox -width 710 -height 25
  cov_create .covbox

  # Create the bottom frame
  panedwindow .bot -width 120 -height 300 -sashrelief raised -sashwidth 4

  # Create frames for pane handle
  frame .bot.left  -relief raised -borderwidth 1
  frame .bot.right -relief raised -borderwidth 1

  # Create the textbox header frame
  frame .bot.right.h
  label .bot.right.h.tl -text "Cur   Line #       Verilog Source" -anchor w
  button .bot.right.h.prev -text "<--" -state disabled -command {
    goto_uncov $prev_uncov_index
  }
  button .bot.right.h.next -text "-->" -state disabled -command {
    goto_uncov $next_uncov_index
  }
  button .bot.right.h.find -text "Find:" -state disabled -command {
    perform_search [.bot.right.h.e get]
  }
  entry .bot.right.h.e -width 15 -relief sunken -state disabled
  bind .bot.right.h.e <Return> {
    perform_search [.bot.right.h.e get]
  }
  button .bot.right.h.clear -text "Clear" -state disabled -command {
    .bot.right.txt tag delete search_found
    .bot.right.h.e delete 0 end
    set start_search_index 1.0
  }

  # Pack the textbox header frame
  pack .bot.right.h.tl    -side left  -fill both
  pack .bot.right.h.clear -side right -fill both
  pack .bot.right.h.e     -side right -fill both
  pack .bot.right.h.find  -side right -fill both
  pack .bot.right.h.next  -side right -fill both
  pack .bot.right.h.prev  -side right -fill both

  # Create the listbox frames and handles
  panedwindow .bot.left.flb -relief raised -borderwidth 1 -sashrelief raised -sashwidth 4
  frame .bot.left.flb.ff   -relief flat
  frame .bot.left.flb.fhmt -relief flat
  frame .bot.left.flb.fp   -relief flat

  # Create the listbox labels
  label .bot.left.flb.ff.ll   -text "Modules" -anchor w -width 30 -borderwidth 1 -relief raised
  label .bot.left.flb.fhmt.ll -text "(H/M/T)" -anchor w -width 11 -borderwidth 1 -relief raised
  label .bot.left.flb.fp.ll   -text "Hit %"   -anchor w -width 5  -borderwidth 1 -relief raised

  # Create the listbox widget to display file names
  listbox .bot.left.flb.ff.l -yscrollcommand listbox_yset -xscrollcommand listbox_xset -width 30 -relief flat -selectborderwidth 0
  listbox .bot.left.flb.fhmt.l -yscrollcommand listbox_yset -xscrollcommand listbox_xset -width 11 -relief flat -selectborderwidth 0 -selectbackground [.bot.left.ff.l cget -background]
  listbox .bot.left.fp.l -yscrollcommand listbox_yset -xscrollcommand listbox_xset -width 5  -relief flat -selectborderwidth 0 -selectbackground [.bot.left.ff.l cget -background]

  label     .bot.left.fvb.ll  -height [.bot.left.flb.ff.ll cget -height]
  scrollbar .bot.left.fvb.lvb -command listbox_yview
  scrollbar .bot.left.lhb -orient horizontal -command listbox_xview

  bind .bot.left.ff.l <<ListboxSelect>> populate_text

  # Create bottom information bar
  label .info -anchor w -relief raised -borderwidth 1

  # Pack the functional unit widgets into the functional unit frame
  pack .bot.left.flb.ff.ll -fill x
  pack .bot.left.flb.ff.l  -fill both -expand yes

  # Pack the hit/miss/total count widgets into the hit/miss/total count frame
  pack .bot.left.flb.fhmt.ll -fill x
  pack .bot.left.flb.fhmt.l  -fill both -expand yes

  # Pack the hit percent widgets into the hit percent frame
  pack .bot.left.flb.fp.ll -fill x
  pack .bot.left.flb.fp.l  -fill both -expand yes

  # Pack the left paned window
  .bot.left.flb add .bot.left.flb.ff
  .bot.left.flb add .bot.left.flb.fhmt
  .bot.left.flb add .bot.left.flb.fp
  
  .bot.left.flb paneconfigure .bot.left.flb.ff   -sticky news -stretch always
  .bot.left.flb paneconfigure .bot.left.flb.fhmt -sticky news
  .bot.left.flb paneconfigure .bot.left.flb.fp   -sticky news -stretch never

  # Pack the vertical scrollbar frame
  pack .bot.left.fvb.ll
  pack .bot.left.fvb.lvb -expand yes

  # Pack the left frame
  grid rowconfigure    .bot.left 0 -weight 1
  grid columnconfigure .bot.left 0 -weight 1
  grid .bot.left.flb -row 0 -column 0 -sticky news
  grid .bot.left.fvb -row 0 -column 1 -sticky ns
  grid .bot.left.lhb -row 1 -column 0 -sticky ew

  # Create the text widget to display the modules/instances
  text .bot.right.txt -yscrollcommand ".bot.right.vb set" -xscrollcommand ".bot.right.hb set" -wrap none -state disabled
  scrollbar .bot.right.vb -command ".bot.right.txt yview"
  scrollbar .bot.right.hb -orient horizontal -command ".bot.right.txt xview"

  # Pack the right paned window
  grid rowconfigure    .bot.right 1 -weight 1
  grid columnconfigure .bot.right 0 -weight 1
  grid .bot.right.h   -row 0 -column 0 -columnspan 2 -sticky nsew
  grid .bot.right.txt -row 1 -column 0 -sticky nsew
  grid .bot.right.vb  -row 1 -column 1 -sticky ns
  grid .bot.right.hb  -row 2 -column 0 -sticky ew

  # Pack the bottom window
  .bot add .bot.left
  .bot add .bot.right

  # Pack the widgets
  pack .bot  -fill both -expand yes
  pack .info -fill both

  # Set the window icon
  global HOME
  wm title . "Covered - Verilog Code Coverage Tool"

  # Set focus on the new window
  #wm attributes . -topmost true
  wm focusmodel . active
  raise .

  # Set icon
  set icon_img [image create photo -file [file join $HOME scripts cov_icon.gif]]
  wm iconphoto . -default $icon_img

}

proc populate_listbox {} {

  global mod_inst_type funit_names funit_types inst_list cdd_name
  global line_summary_total line_summary_hit
  global toggle_summary_total toggle_summary_hit
  global uncov_fgColor uncov_bgColor
  global lb_fgColor lb_bgColor
  global summary_list
 
  # Remove contents currently in listboxes
  set lb_size [.bot.left.flb.ff.l size]
  .bot.left.flb.ff.l   delete 0 $lb_size
  .bot.left.flb.fhmt.l delete 0 $lb_size
  .bot.left.flb.fp.l   delete 0 $lb_size

  # Clear funit_names and funit_types values
  set funit_names ""
  set funit_types ""

  if {$cdd_name != ""} {

    # If we are in module mode, list modules (otherwise, list instances)
    if {$mod_inst_type == "module"} {

      # Get the list of functional units
      tcl_func_get_funit_list 

      # Calculate the summary_list array
      calculate_summary

      for {set i 0} {$i < [llength $summary_list]} {incr i} {
        set funit [lindex $summary_list $i]
        .bot.left.flb.ff.l   insert end [lindex $funit 0]
        .bot.left.flb.ff.l   itemconfigure $i -background [lindex $funit 5]
        .bot.left.flb.fhmt.l insert end "[lindex $funit 1]/[lindex $funit 2]/[lindex $funit 3]"
        .bot.left.flb.fp.l   insert end "[lindex $funit 4]%"
      }

      .bot.left.ff.ll configure -text "Modules"

    } else {

      set inst_list ""
      tcl_func_get_instance_list

      foreach inst_name $inst_list {
        $listbox_w insert end $inst_name
      }

      .bot.left.flb.ff.ll configure -text "Instances"

    }

  }

}

proc populate_text {} {

  global cov_rb mod_inst_type funit_names funit_types
  global curr_funit_name curr_funit_type last_lb_index
  global start_search_index
  global curr_toggle_ptr

  set index [.bot.left.ff.l curselection]

  if {$index != ""} {

    if {$last_lb_index != $index} {

      set last_lb_index $index
      set curr_funit_name [lindex $funit_names $index]
      set curr_funit_type [lindex $funit_types $index]
      set curr_toggle_ptr ""

      if {$cov_rb == "Line"} {
        process_funit_line_cov
      } elseif {$cov_rb == "Toggle"} {
        process_funit_toggle_cov
      } elseif {$cov_rb == "Memory"} {
        process_funit_memory_cov
      } elseif {$cov_rb == "Logic"} {
        process_funit_comb_cov
      } elseif {$cov_rb == "FSM"} {
        process_funit_fsm_cov
      } elseif {$cov_rb == "Assert"} {
        process_funit_assert_cov
      } else {
        # ERROR
      }

      # Reset starting search index
      set start_search_index 1.0
      set curr_uncov_index   ""

      # Run initial goto_uncov to initialize previous and next pointers
      goto_uncov $curr_uncov_index

      # Enable widgets
      .bot.right.h.e     configure -state normal -bg white
      .bot.right.h.find  configure -state normal
      .bot.right.h.clear configure -state normal

    }

  }

}

proc clear_text {} {

  global last_lb_index

  # Clear the textbox
  .bot.right.txt configure -state normal
  .bot.right.txt delete 1.0 end
  .bot.right.txt configure -state disabled

  # Clear the summary info
  cov_clear_summary

  # Reset the last_lb_index
  set last_lb_index ""

}

proc perform_search {value} {

  global start_search_index

  set index [.bot.right.txt search $value $start_search_index]

  # Delete search_found tag
  .bot.right.txt tag delete search_found

  if {$index != ""} {

    # Highlight found text
    set value_len [string length $value]
    .bot.right.txt tag add search_found $index "$index + $value_len chars"
    .bot.right.txt tag configure search_found -background orange1

    # Make the text visible
    .bot.right.txt see $index 

    # Calculate next starting index
    set indices [split $index .]
    set start_search_index [lindex $indices 0].[expr [lindex $indices 1] + 1]

  } else {

    # Output a message specifying that the searched string could not be found
    tk_messageBox -message "String \"$value\" not found" -type ok -parent .

    # Clear the contents of the search entry box
    .bot.right.h.e delete 0 end

    # Reset search index
    set start_search_index 1.0

  }

  # Set focus to text box
  focus .bot.right.txt

  return 1

}

proc rm_pointer {curr_ptr} {

  upvar $curr_ptr ptr

  # Allow the textbox to be changed
  .bot.right.txt configure -state normal

  # Delete old cursor, if it is displayed
  if {$ptr != ""} {
    .bot.right.txt delete $ptr.0 $ptr.3
    .bot.right.txt insert $ptr.0 "   "
  }

  # Disable textbox
  .bot.right.txt configure -state disabled

  # Disable "Show current selection" menu item
  .menubar.view entryconfigure 4 -state disabled

  # Clear current pointer
  set ptr ""

}

proc set_pointer {curr_ptr line} {

  upvar $curr_ptr ptr

  # Remove old pointer
  rm_pointer ptr

  # Allow the textbox to be changed
  .bot.right.txt configure -state normal

  # Display new pointer
  .bot.right.txt delete $line.0 $line.3
  .bot.right.txt insert $line.0 "-->"

  # Set the textbox to not be changed
  .bot.right.txt configure -state disabled

  # Make sure that we can see the current toggle pointer in the textbox
  .bot.right.txt see $line.0

  # Enable the "Show current selection" menu option
  .menubar.view entryconfigure 4 -state normal

  # Set the current pointer to the specified line
  set ptr $line

}

proc goto_uncov {curr_index} {

  global prev_uncov_index next_uncov_index curr_uncov_index
  global cov_rb

  # Calculate the name of the tag to use
  if {$cov_rb == "Line"} {
    set tag_name "uncov_colorMap"
  } else {
    set tag_name "uncov_button"
  }

  # Display the given index, if it has been specified
  if {$curr_index != ""} {
    .bot.right.txt see $curr_index
    set curr_uncov_index $curr_index
  } else {
    set curr_uncov_index 1.0
  }

  # Get previous uncovered index
  set prev_uncov_index [lindex [.bot.right.txt tag prevrange $tag_name $curr_uncov_index] 0]

  # Disable/enable previous button
  if {$prev_uncov_index != ""} {
    .bot.right.h.prev configure -state normal
    .menubar.view entryconfigure 3 -state normal
  } else {
    .bot.right.h.prev configure -state disabled
    .menubar.view entryconfigure 3 -state disabled
  }

  # Get next uncovered index
  set next_uncov_index [lindex [.bot.right.txt tag nextrange $tag_name "$curr_uncov_index + 1 chars"] 0]

  # Disable/enable next button
  if {$next_uncov_index != ""} {
    .bot.right.h.next configure -state normal
    .menubar.view entryconfigure 2 -state normal
  } else {
    .bot.right.h.next configure -state disabled
    .menubar.view entryconfigure 2 -state disabled
  }

}

proc update_all_windows {} {

  global curr_uncov_index

  # Update the main window
  goto_uncov $curr_uncov_index
  .menubar.view entryconfigure 4 -state disabled

  # Update the summary window
  update_summary

  # Update the toggle window
  update_toggle

  # Update the memory window
  update_memory

  # Update the combinational logic window
  update_comb

  # Update the FSM window
  update_fsm

  # Update the assertion window
  update_assert

}

proc clear_all_windows {} {

  # Clear the main window
  clear_text

  # Clear the summary window
  clear_summary

  # Clear the toggle window
  clear_toggle

  # Clear the memory window
  clear_memory

  # Clear the combinational logic window
  clear_comb

  # Clear the FSM window
  clear_fsm

  # Clear the assertion window
  clear_assert

}

proc bgerror {msg} {

  ;# Remove the status window if it exists
  destroy .status

  ;# Display error message
  set retval [tk_messageBox -message $msg -title "Error" -type ok]

}

proc listbox_yset {args} {
    
  eval [linsert $args 0 .bot.left.fvb.lvb set]
  listbox_yview moveto [lindex [.bot.left.fvb.lvb get] 0]

} 
  
proc listbox_yview {args} {

  eval [linsert $args 0 .bot.left.ff.l   yview]
  eval [linsert $args 0 .bot.left.fhmt.l yview]
  eval [linsert $args 0 .bot.left.fp.l   yview]

}

proc listbox_xset {args} {

  eval [linsert $args 0 .bot.left.lhb set]
  listbox_xview moveto [lindex [.bot.left.lhb get] 0]

}

proc listbox_xview {args} {

  eval [linsert $args 0 .bot.left.ff.l   xview]
  eval [linsert $args 0 .bot.left.fhmt.l xview]
  eval [linsert $args 0 .bot.left.fp.l   xview]

}


# Read configuration file
read_coveredrc

# Display main window
main_view
