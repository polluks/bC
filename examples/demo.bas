rem bC demo: move player0 with joystick 0, player1 chases randomly
rem touch player1 to score

dim frame = $80

const p0color = $1E
const p1color = $46

score = 0
player0x = 40 : player0y = 60
player1x = 100 : player1y = 90

COLUP0 = p0color : COLUP1 = p1color

player0:
%00111100
%01111110
%11011011
%11111111
%11111111
%01111110
%00100100
%01000010
end

player1:
%11111111
%10111101
%10011001
%11100111
%11100111
%10011001
%10111101
%11111111
end

playfield:
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
X..............................X
X..............................X
X..............................X
X..............................X
X..............................X
X..............................X
X..............................X
X..............................X
X..............................X
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
end

mainloop
 if joy0left then player0x = player0x - 1
 if joy0right then player0x = player0x + 1
 if joy0up then player0y = player0y - 1
 if joy0down then player0y = player0y + 1
 if collision(player0, playfield) then gosub bump
 frame = frame + 1
 if frame > 30 then frame = 0 : b = rand & 3 : if b = 0 then player1x = player1x + 2 : if b = 1 then player1x = player1x - 2
 if collision(player0, player1) then score = score + 10
 drawscreen
 goto mainloop

bump
 player0x = player0x + 3
 pfpixel (rand & 31) (rand & 7) flip
 return
