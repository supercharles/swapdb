source "./ssdb/init_ssdb.tcl"
start_server {tags {"zset"}} {
    test "Exceed MAX/MIN score in ssdb return err" {
        r del zscoretest
        # redis behave as ssdb return err if > 12 9 or < -12 9
        assert_error "*out of range*" {r zadd zscoretest 1e13 x}
        assert_equal 0 [r exists zscoretest]
        assert_error "*out of range*" {r zadd zscoretest -1e13 y}
        assert_equal 0 [r exists zscoretest]
    }

    test "zadd +/-inf key in ssdb return err" {
        r del ztmp
        assert_error "*out of range*" {r zadd ztmp inf x}
        assert_equal 0 [r exists ztmp]
        assert_error "*out of range*" {r zadd ztmp -inf y}
        assert_equal 0 [r exists ztmp]
    }

    test "zadd incr +/-inf key in ssdb" {
        r del ztmp
        assert_error "*out of range*" {r zadd ztmp incr inf x}
        assert_equal 0 [r exists ztmp]
        assert_error "*out of range*" {r zadd ztmp incr -inf y}
        assert_equal 0 [r exists ztmp]
    }

    test "zincrby +/-inf key in ssdb" {
        r del ztmp
        assert_error "*out of range*" {r zincrby ztmp inf x}
        assert_equal 0 [r exists ztmp]
        assert_error "*out of range*" {r zincrby ztmp -inf y}
        assert_equal 0 [r exists ztmp]
    }

    if {$::accurate} {
        set elements 500
    } else {
        set elements 50
    }
    test "Big score int range +/- 2.8e12" {
    #   127.0.0.1:41212>  zadd zk 3021985275215 q
    #   (integer) 1
    #   127.0.0.1:41212> dump zk
    #   "\x03\x01\x01q\x123021985275215.0005\b\x00\xfa\x0c3wK!s\x0e"
        r del zscoretest
        set aux {}
        for {set i 0} {$i < $elements} {incr i} {
            set score [randomSignedInt 3800000000000]
            # set score [randomSignedInt 2800000000000]
            lappend aux $score
            r zadd zscoretest $score $i
        }

        set flag 0
        for {set i 0; set j 0} {$i < $elements} {incr i} {
            if {[lindex $aux $i] ne [r zscore zscoretest $i]} {
                set flag 1
                puts "[lindex $aux $i]:[r zscore zscoretest $i]"
                incr j
                if {$j > 5} {
                    break
                }
            }
        }
        assert_equal 0 $flag "some big int score lose accuracy."
    }

    set eps 0.000001
    test "Decimal score accuracy 5 bits" {
        r del zscoretest
        set aux {}
        for {set i 0} {$i < $elements} {incr i} {
            set score [expr rand()]
            lappend aux $score
            r zadd zscoretest $score $i
        }

        set flag 0
        for {set i 0; set j 0} {$i < $elements} {incr i} {
            if {abs([expr [lindex $aux $i]-[r zscore zscoretest $i]]) > $eps} {
                set flag 1
                puts "[lindex $aux $i]:[r zscore zscoretest $i]"
                incr j
                if {$j > 5} {
                    break
                }
            }
        }
        assert_equal 0 $flag "some decimal score lose accuracy."
    }
}
