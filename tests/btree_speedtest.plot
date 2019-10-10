# Gnuplot/SqlPlotTools scripts for btree_speedtest.cpp

# IMPORT-DATA stats btree_speedtest_results.txt

set terminal pdf size 18.6cm,12cm linewidth 2.0 noenhanced
set output "btree_speedtest_results.pdf"

set pointsize 0.7
set style line 6 lc rgb "#f0b000"
set style line 15 lc rgb "#f0b000"
set style line 24 lc rgb "#f0b000"
set style line 33 lc rgb "#f0b000"
set style line 42 lc rgb "#f0b000"
set style line 51 lc rgb "#f0b000"
set style line 60 lc rgb "#f0b000"
set style increment user

set grid xtics ytics

set key top left reverse Left

set title 'tlx::btree_multiset vs std::multiset vs std::unordered_multiset -- Insert Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'
set yrange [*:2000]

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'set_insert'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 0 title "container=std::multiset" with linespoints, \
    'btree_speedtest-data.txt' index 1 title "container=std::unordered_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 2 title "container=tlx::splay_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 3 title "container=reservoir::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 4 title "container=tlx::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 5 title "container=reservoir::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 6 title "container=tlx::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 7 title "container=reservoir::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 8 title "container=tlx::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 9 title "container=reservoir::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 10 title "container=tlx::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 11 title "container=reservoir::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 12 title "container=tlx::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 13 title "container=reservoir::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 14 title "container=tlx::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 15 title "container=reservoir::btree_multiset<256>" with linespoints, \
    'btree_speedtest-data.txt' index 16 title "container=tlx::btree_multiset<256>" with linespoints

set title 'tlx::btree_multiset vs std::multiset vs std::unordered_multiset -- Find Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'
set yrange [*:1600]

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'set_find'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 17 title "container=std::multiset" with linespoints, \
    'btree_speedtest-data.txt' index 18 title "container=std::unordered_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 19 title "container=tlx::splay_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 20 title "container=reservoir::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 21 title "container=tlx::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 22 title "container=reservoir::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 23 title "container=tlx::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 24 title "container=reservoir::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 25 title "container=tlx::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 26 title "container=reservoir::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 27 title "container=tlx::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 28 title "container=reservoir::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 29 title "container=tlx::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 30 title "container=reservoir::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 31 title "container=tlx::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 32 title "container=reservoir::btree_multiset<256>" with linespoints, \
    'btree_speedtest-data.txt' index 33 title "container=tlx::btree_multiset<256>" with linespoints

set title 'tlx::btree_multiset vs std::multiset vs std::unordered_multiset -- Insert/Find/Delete Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'
set yrange [*:5000]

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'set_insert_find_delete'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 34 title "container=std::multiset" with linespoints, \
    'btree_speedtest-data.txt' index 35 title "container=std::unordered_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 36 title "container=tlx::splay_multiset" with linespoints, \
    'btree_speedtest-data.txt' index 37 title "container=reservoir::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 38 title "container=tlx::btree_multiset<4>" with linespoints, \
    'btree_speedtest-data.txt' index 39 title "container=reservoir::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 40 title "container=tlx::btree_multiset<8>" with linespoints, \
    'btree_speedtest-data.txt' index 41 title "container=reservoir::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 42 title "container=tlx::btree_multiset<16>" with linespoints, \
    'btree_speedtest-data.txt' index 43 title "container=reservoir::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 44 title "container=tlx::btree_multiset<32>" with linespoints, \
    'btree_speedtest-data.txt' index 45 title "container=reservoir::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 46 title "container=tlx::btree_multiset<64>" with linespoints, \
    'btree_speedtest-data.txt' index 47 title "container=reservoir::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 48 title "container=tlx::btree_multiset<128>" with linespoints, \
    'btree_speedtest-data.txt' index 49 title "container=reservoir::btree_multiset<256>" with linespoints, \
    'btree_speedtest-data.txt' index 50 title "container=tlx::btree_multiset<256>" with linespoints


################################################################################

set title 'tlx::btree_multimap vs std::multimap vs std::unordered_multimap -- Insert Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'
set yrange [*:*]

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'map_insert'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 51 title "container=std::multimap" with linespoints, \
    'btree_speedtest-data.txt' index 52 title "container=std::unordered_multimap" with linespoints, \
    'btree_speedtest-data.txt' index 53 title "container=reservoir::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 54 title "container=tlx::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 55 title "container=reservoir::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 56 title "container=tlx::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 57 title "container=reservoir::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 58 title "container=tlx::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 59 title "container=reservoir::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 60 title "container=tlx::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 61 title "container=reservoir::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 62 title "container=tlx::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 63 title "container=reservoir::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 64 title "container=tlx::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 65 title "container=reservoir::btree_multimap<256>" with linespoints, \
    'btree_speedtest-data.txt' index 66 title "container=tlx::btree_multimap<256>" with linespoints

set title 'tlx::btree_multimap vs std::multimap vs std::unordered_multimap -- Find Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'map_find'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 67 title "container=std::multimap" with linespoints, \
    'btree_speedtest-data.txt' index 68 title "container=std::unordered_multimap" with linespoints, \
    'btree_speedtest-data.txt' index 69 title "container=reservoir::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 70 title "container=tlx::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 71 title "container=reservoir::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 72 title "container=tlx::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 73 title "container=reservoir::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 74 title "container=tlx::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 75 title "container=reservoir::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 76 title "container=tlx::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 77 title "container=reservoir::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 78 title "container=tlx::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 79 title "container=reservoir::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 80 title "container=tlx::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 81 title "container=reservoir::btree_multimap<256>" with linespoints, \
    'btree_speedtest-data.txt' index 82 title "container=tlx::btree_multimap<256>" with linespoints

set title 'tlx::btree_multimap vs std::multimap vs std::unordered_multimap -- Insert/Find/Delete Performance Test'
set xlabel 'Item Count [log_2(n)]'
set ylabel 'Time per Item [Nanoseconds / Item]'

## MULTIPLOT(container)
## SELECT LOG(2, items) AS x, MEDIAN(time / items * 1e9) AS y, MULTIPLOT
## FROM stats
## WHERE op = 'map_insert_find_delete'
## GROUP BY MULTIPLOT,slots,x ORDER BY slots,container,x
plot \
    'btree_speedtest-data.txt' index 83 title "container=std::multimap" with linespoints, \
    'btree_speedtest-data.txt' index 84 title "container=std::unordered_multimap" with linespoints, \
    'btree_speedtest-data.txt' index 85 title "container=reservoir::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 86 title "container=tlx::btree_multimap<4>" with linespoints, \
    'btree_speedtest-data.txt' index 87 title "container=reservoir::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 88 title "container=tlx::btree_multimap<8>" with linespoints, \
    'btree_speedtest-data.txt' index 89 title "container=reservoir::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 90 title "container=tlx::btree_multimap<16>" with linespoints, \
    'btree_speedtest-data.txt' index 91 title "container=reservoir::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 92 title "container=tlx::btree_multimap<32>" with linespoints, \
    'btree_speedtest-data.txt' index 93 title "container=reservoir::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 94 title "container=tlx::btree_multimap<64>" with linespoints, \
    'btree_speedtest-data.txt' index 95 title "container=reservoir::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 96 title "container=tlx::btree_multimap<128>" with linespoints, \
    'btree_speedtest-data.txt' index 97 title "container=reservoir::btree_multimap<256>" with linespoints, \
    'btree_speedtest-data.txt' index 98 title "container=tlx::btree_multimap<256>" with linespoints
