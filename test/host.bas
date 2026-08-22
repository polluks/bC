rem bC host-logic verification program

data tbl
10
20
end

rem ---- gosub / return chain (runs once, before main loop) ----
gosub sub1
d = d + 100
goto done

sub1
 e = e + 1
 gosub sub2
 e = e + 10
 return

sub2
 f = f + 1
 return

done
 h = 1
 t = 0
 on h gosub rr
 n = n + 1
 on f goto gg1 gg2

rr
 t = t + 5
 return

gg1
 o = o + 50
 goto fin

gg2
 o = o + 99

fin
 rem ---- loops ----
 p = 0
 for q = 1 to 10 step 3
   p = p + 1
 next
 r = 5
 do
   r = r - 1
 loop until r = 0
 w = 3
 while w > 0
   w = w - 1
 wend
 x = 0
 do
   x = x + 1
   if x = 3 then exit
 loop

 rem ---- runtime helpers ----
 y = pfread(10, 10)
 z = sread(tbl)
 v = sread(tbl)
 score = 0
 score = score + 250
 score = score + 250

 rem ---- per-frame input/collision section ----
mloop
 if joy0right then m = m + 1
 if collision(player0, player1) then c = c + 1
 drawscreen
 goto mloop
