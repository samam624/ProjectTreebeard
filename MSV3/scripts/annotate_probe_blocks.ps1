param(
    [string]$FrontSource = (Join-Path $PSScriptRoot '..\docs\MSV3_PROBE_FRONT_ORIGINAL.png'),
    [string]$BackSource = (Join-Path $PSScriptRoot '..\docs\MSV3_PROBE_BACK_ORIGINAL.png'),
    [string]$FrontOutput = (Join-Path $PSScriptRoot '..\docs\MSV3_PROBE_FRONT_FITTED_REGIONS_ANNOTATED.png'),
    [string]$BackOutput = (Join-Path $PSScriptRoot '..\docs\MSV3_PROBE_BACK_FITTED_REGIONS_ANNOTATED.png')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

function Add-Callout {
    param(
        [Parameter(Mandatory)] [System.Drawing.Graphics]$Graphics,
        [Parameter(Mandatory)] [System.Drawing.Font]$Font,
        [Parameter(Mandatory)] [string[]]$Text,
        [Parameter(Mandatory)] [System.Drawing.RectangleF]$Box,
        [Parameter(Mandatory)] [System.Drawing.PointF]$Anchor,
        [Parameter(Mandatory)] [ValidateSet('Left', 'Right')] [string]$Side,
        [Parameter(Mandatory)] [System.Drawing.Color]$Color,
        [System.Drawing.PointF[]]$ExtraAnchors = @()
    )

    $border = [System.Drawing.Color]::FromArgb(190, 20, 20, 20)
    $fill = [System.Drawing.Color]::FromArgb(238, 255, 255, 255)
    $textColor = [System.Drawing.Color]::FromArgb(255, 18, 18, 18)

    $linePen = [System.Drawing.Pen]::new($Color, 2.0)
    $borderPen = [System.Drawing.Pen]::new($border, 1.0)
    $fillBrush = [System.Drawing.SolidBrush]::new($fill)
    $textBrush = [System.Drawing.SolidBrush]::new($textColor)
    $dotBrush = [System.Drawing.SolidBrush]::new($Color)
    try {
        $linePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $linePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

        $edgeX = if ($Side -eq 'Left') { $Box.Right } else { $Box.Left }
        $edgeY = $Box.Top + ($Box.Height / 2.0)
        $edge = [System.Drawing.PointF]::new($edgeX, $edgeY)

        $Graphics.DrawLine($linePen, $edge, $Anchor)
        foreach ($extraAnchor in $ExtraAnchors) {
            $Graphics.DrawLine($linePen, $edge, $extraAnchor)
        }

        $Graphics.FillRectangle($fillBrush, $Box)
        $Graphics.DrawRectangle($borderPen, $Box.X, $Box.Y, $Box.Width, $Box.Height)

        $format = [System.Drawing.StringFormat]::new()
        try {
            $format.Alignment = [System.Drawing.StringAlignment]::Center
            $format.LineAlignment = [System.Drawing.StringAlignment]::Center
            $Graphics.DrawString(($Text -join "`n"), $Font, $textBrush, $Box, $format)
        } finally {
            $format.Dispose()
        }

        foreach ($point in @($Anchor) + $ExtraAnchors) {
            $Graphics.FillEllipse($dotBrush, $point.X - 3.5, $point.Y - 3.5, 7.0, 7.0)
        }
    } finally {
        $linePen.Dispose()
        $borderPen.Dispose()
        $fillBrush.Dispose()
        $textBrush.Dispose()
        $dotBrush.Dispose()
    }
}

function Add-Section {
    param(
        [Parameter(Mandatory)] [System.Drawing.Graphics]$Graphics,
        [Parameter(Mandatory)] [System.Drawing.Font]$Font,
        [Parameter(Mandatory)] [string[]]$Text,
        [Parameter(Mandatory)] [System.Drawing.RectangleF]$Boundary,
        [Parameter(Mandatory)] [System.Drawing.RectangleF]$LabelBox,
        [Parameter(Mandatory)] [System.Drawing.Color]$Color,
        [ValidateSet('Ellipse', 'Rectangle', 'RoundedRectangle', 'Polygon')] [string]$Shape = 'Ellipse',
        [System.Drawing.PointF[]]$Points = @()
    )

    $outline = [System.Drawing.Pen]::new($Color, 2.6)
    $regionFill = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(32, $Color.R, $Color.G, $Color.B))
    $labelFill = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(238, 255, 255, 255))
    $labelText = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 18, 18, 18))
    try {
        $outline.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Solid
        switch ($Shape) {
            'Ellipse' {
                $Graphics.FillEllipse($regionFill, $Boundary)
                $Graphics.DrawEllipse($outline, $Boundary)
            }
            'Rectangle' {
                $Graphics.FillRectangle($regionFill, $Boundary)
                $Graphics.DrawRectangle($outline, $Boundary.X, $Boundary.Y, $Boundary.Width, $Boundary.Height)
            }
            'RoundedRectangle' {
                $radius = 12.0
                $diameter = $radius * 2.0
                $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
                try {
                    $path.AddArc($Boundary.Left, $Boundary.Top, $diameter, $diameter, 180, 90)
                    $path.AddArc($Boundary.Right - $diameter, $Boundary.Top, $diameter, $diameter, 270, 90)
                    $path.AddArc($Boundary.Right - $diameter, $Boundary.Bottom - $diameter, $diameter, $diameter, 0, 90)
                    $path.AddArc($Boundary.Left, $Boundary.Bottom - $diameter, $diameter, $diameter, 90, 90)
                    $path.CloseFigure()
                    $Graphics.FillPath($regionFill, $path)
                    $Graphics.DrawPath($outline, $path)
                } finally {
                    $path.Dispose()
                }
            }
            'Polygon' {
                if ($Points.Count -lt 3) { throw 'A polygon section requires at least three points.' }
                $Graphics.FillPolygon($regionFill, $Points)
                $Graphics.DrawPolygon($outline, $Points)
            }
        }

        $Graphics.FillRectangle($labelFill, $LabelBox)
        $Graphics.DrawRectangle($outline, $LabelBox.X, $LabelBox.Y, $LabelBox.Width, $LabelBox.Height)
        $format = [System.Drawing.StringFormat]::new()
        try {
            $format.Alignment = [System.Drawing.StringAlignment]::Center
            $format.LineAlignment = [System.Drawing.StringAlignment]::Center
            $Graphics.DrawString(($Text -join "`n"), $Font, $labelText, $LabelBox, $format)
        } finally {
            $format.Dispose()
        }
    } finally {
        $outline.Dispose()
        $regionFill.Dispose()
        $labelFill.Dispose()
        $labelText.Dispose()
    }
}

function New-AnnotatedImage {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Output,
        [Parameter(Mandatory)] [scriptblock]$Draw
    )

    $resolvedSource = (Resolve-Path -LiteralPath $Source).Path
    $bitmap = [System.Drawing.Bitmap]::new($resolvedSource)
    try {
        # Draw directly on an exact-size copy. No resize, filtering, regeneration, or
        # geometric transformation is performed on the source image.
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $font = [System.Drawing.Font]::new('Segoe UI', 10.0, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
            try {
                & $Draw $graphics $font
            } finally {
                $font.Dispose()
            }
        } finally {
            $graphics.Dispose()
        }

        $absoluteOutput = [System.IO.Path]::GetFullPath($Output)
        $bitmap.Save($absoluteOutput, [System.Drawing.Imaging.ImageFormat]::Png)
        [PSCustomObject]@{
            Source = $resolvedSource
            Output = $absoluteOutput
            Width = $bitmap.Width
            Height = $bitmap.Height
        }
    } finally {
        $bitmap.Dispose()
    }
}

$front = New-AnnotatedImage -Source $FrontSource -Output $FrontOutput -Draw {
    param($graphics, $font)

    $mcuColor = [System.Drawing.Color]::FromArgb(145, 70, 205)
    $usbColor = [System.Drawing.Color]::FromArgb(0, 115, 205)
    $powerColor = [System.Drawing.Color]::FromArgb(225, 115, 15)
    $sensorColor = [System.Drawing.Color]::FromArgb(0, 145, 135)
    $solarColor = [System.Drawing.Color]::FromArgb(190, 120, 0)

    Add-Section -Graphics $graphics -Font $font -Text @('MCU + BLE', 'E73-2G4M08S1C') `
        -Boundary ([System.Drawing.RectangleF]::new(0, 0, 0, 0)) `
        -Points @(
            [System.Drawing.PointF]::new(143, 77),
            [System.Drawing.PointF]::new(276, 77),
            [System.Drawing.PointF]::new(276, 254),
            [System.Drawing.PointF]::new(270, 263),
            [System.Drawing.PointF]::new(190, 263),
            [System.Drawing.PointF]::new(190, 274),
            [System.Drawing.PointF]::new(143, 274)
        ) -Shape Polygon -LabelBox ([System.Drawing.RectangleF]::new(4, 94, 112, 40)) -Color $mcuColor

    Add-Section -Graphics $graphics -Font $font -Text @('USB DATA', '+ CHARGING') `
        -Boundary ([System.Drawing.RectangleF]::new(0, 0, 0, 0)) `
        -Points @(
            [System.Drawing.PointF]::new(286, 146),
            [System.Drawing.PointF]::new(336, 146),
            [System.Drawing.PointF]::new(336, 539),
            [System.Drawing.PointF]::new(259, 539),
            [System.Drawing.PointF]::new(259, 438),
            [System.Drawing.PointF]::new(228, 438),
            [System.Drawing.PointF]::new(228, 366),
            [System.Drawing.PointF]::new(241, 366),
            [System.Drawing.PointF]::new(241, 269),
            [System.Drawing.PointF]::new(286, 269)
        ) -Shape Polygon -LabelBox ([System.Drawing.RectangleF]::new(337, 155, 103, 42)) -Color $usbColor

    Add-Section -Graphics $graphics -Font $font -Text @('SOLAR INPUT', 'SP+ / GSP + C4') `
        -Boundary ([System.Drawing.RectangleF]::new(0, 0, 0, 0)) `
        -Points @(
            [System.Drawing.PointF]::new(146, 280),
            [System.Drawing.PointF]::new(224, 280),
            [System.Drawing.PointF]::new(224, 390),
            [System.Drawing.PointF]::new(146, 390)
        ) -Shape Polygon -LabelBox ([System.Drawing.RectangleF]::new(4, 304, 110, 42)) -Color $solarColor

    Add-Section -Graphics $graphics -Font $font -Text @('BATTERY + POWER', 'SOURCE SELECT') `
        -Boundary ([System.Drawing.RectangleF]::new(0, 0, 0, 0)) `
        -Points @(
            [System.Drawing.PointF]::new(108, 443),
            [System.Drawing.PointF]::new(255, 443),
            [System.Drawing.PointF]::new(255, 572),
            [System.Drawing.PointF]::new(108, 572)
        ) -Shape Polygon -LabelBox ([System.Drawing.RectangleF]::new(4, 448, 101, 42)) -Color $powerColor

    Add-Section -Graphics $graphics -Font $font -Text @('SENSING ELECTRONICS', 'TMUX1109PWR', 'FDC2214RGHR', 'L2 / C17 LC TANK') `
        -Boundary ([System.Drawing.RectangleF]::new(0, 0, 0, 0)) `
        -Points @(
            [System.Drawing.PointF]::new(259, 544),
            [System.Drawing.PointF]::new(336, 544),
            [System.Drawing.PointF]::new(336, 649),
            [System.Drawing.PointF]::new(145, 649),
            [System.Drawing.PointF]::new(145, 577),
            [System.Drawing.PointF]::new(259, 577)
        ) -Shape Polygon -LabelBox ([System.Drawing.RectangleF]::new(337, 550, 103, 66)) -Color $sensorColor

    Add-Section -Graphics $graphics -Font $font -Text @('SENSOR PLATE', 'MS_NODE_A / B') `
        -Boundary ([System.Drawing.RectangleF]::new(128, 653, 174, 756)) `
        -LabelBox ([System.Drawing.RectangleF]::new(4, 805, 111, 42)) -Color $sensorColor -Shape Rectangle
}

$back = New-AnnotatedImage -Source $BackSource -Output $BackOutput -Draw {
    param($graphics, $font)

    $solarColor = [System.Drawing.Color]::FromArgb(190, 120, 0)

    Add-Section -Graphics $graphics -Font $font -Text @('SOLAR HARVESTER', 'BQ25504RGTR + L1', 'R9-R15 / C8-C11') `
        -Boundary ([System.Drawing.RectangleF]::new(196, 378, 168, 198)) `
        -Shape RoundedRectangle -LabelBox ([System.Drawing.RectangleF]::new(4, 382, 108, 52)) -Color $solarColor
}

$front
$back
