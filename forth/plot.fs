: _plot-input-check ( -- ) \ ( x0 x1 step -- )
  f{: x0 x1 step -- f:}
  step f0<= \ step > 0
  x0 x1 step 2.0 f* frot f+ f>= \ x0 + 2 * step < x1
  invert or if abort" incorrect step" then

  x0 x1 f>= \ x0 < x1
  if abort" incorrect x0 x1" then
;

create _plot-vals 255 floats allot

: _plot-calc-vals ( func -- n ) \ ( x0 x1 step -- )
  f{: x0 x1 step -- f:}
  {: func -- n :}
  0 -> n
  begin x0 x1 step n s>f f* frot f+ f>= while
    x0 step n s>f f* f+ func execute
    _plot-vals n floats + f!
    n 1 + -> n
  repeat
  n
;

: _plot-val-limits ( addr n -- ) \ ( -- min max )
  f{: -- min max f:}
  over f@ fdup f-> min f-> max
  dup 1 > if
    1 do
      dup i floats + f@ fdup max f>
      if f-> max
      else
        fdup min f<
        if f-> min
        else fdrop
        then
      then
    loop
  else drop then
  drop
  min
  max
;

: _plot-num>str ( -- addr len ) \ ( fval -- )
  f{: fval -- f:}
  fval f0<
  fval fabs 100.0 f* fround f>s
  {: neg scaled -- :}
  <#
    scaled # #
    ascii . hold
    dup 0= if ascii 0 hold else #s then
    neg sign
  #>
;

: _plot-scale ( plo phi -- pixel ) \ ( vlo vhi fval -- )
  f{: vlo vhi fval -- f:}
  {: plo phi -- :}
  fval vlo f- vhi vlo f- f/ phi plo - s>f f* plo s>f f+ fround f>s
;

2 constant PLOT-X0
width 2 - constant PLOT-X1
20 constant PLOT-Y0
height 20 - constant PLOT-Y1

: _plot-draw-axes ( -- )  \ ( x0 x1 min max -- )
  f{: x0 x1 min max -- f:}
  PLOT-X0 PLOT-Y0 PLOT-X1 PLOT-Y0 WHITE line \ top
  PLOT-X0 PLOT-Y1 PLOT-X1 PLOT-Y1 WHITE line \ bottom
  PLOT-X0 PLOT-Y0 PLOT-X0 PLOT-Y1 WHITE line \ left
  PLOT-X1 PLOT-Y0 PLOT-X1 PLOT-Y1 WHITE line \ right

  max _plot-num>str 5 PLOT-Y0 15 - WHITE text
  min _plot-num>str 5 PLOT-Y1 15 - WHITE text
  x0 _plot-num>str PLOT-X0 PLOT-Y1 5 + WHITE text
  x1 _plot-num>str PLOT-X1 40 - PLOT-Y1 5 + WHITE text
;

: _plot-draw-points ( color n -- )  \ ( x0 x1 step min max -- )
  f{: x0 x1 step min max -- f:}
  {: color n -- px py :}
  n 0 do
    x0 x1 x0 step i s>f f* f+ PLOT-X0 PLOT-X1 _plot-scale
    min max _plot-vals i floats + f@ PLOT-Y1 PLOT-Y0 _plot-scale
    {: x y -- :}
    i 0 > if px py x y color line then
    x -> px
    y -> py
    x y 2 color -1 circle
  loop
;

: plot ( func color -- ) \ ( x0 x1 step -- )
  f{: x0 x1 step -- f:}
  {: func color -- n :}
  >graph clear
  x0 x1 step _plot-input-check
  x0 x1 step func _plot-calc-vals -> n

  f{: -- min max f:}
  _plot-vals n _plot-val-limits f-> max f-> min
 
  x0 x1 min max _plot-draw-axes
  x0 x1 step min max color n _plot-draw-points
  show
;
