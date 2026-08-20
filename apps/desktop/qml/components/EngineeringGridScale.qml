import QtQml

QtObject {
    id: root

    required property real referenceSpacing
    required property real pixelsPerUnit
    property real minimumSpacingPixels: 18
    readonly property real spacing: chooseSpacing(referenceSpacing,
                                                   pixelsPerUnit,
                                                   minimumSpacingPixels)
    readonly property real spacingPixels: spacing * pixelsPerUnit

    function oneTwoFiveCeiling(value) {
        if (!Number.isFinite(value) || value <= 0)
            return 1
        const exponent = Math.floor(Math.log10(value))
        const decade = Math.pow(10, exponent)
        const normalized = value / decade
        const mantissa = normalized <= 1 ? 1
                         : (normalized <= 2 ? 2
                            : (normalized <= 5 ? 5 : 10))
        return mantissa * decade
    }

    function chooseSpacing(reference, pixelsPerUnitValue, minimumPixels) {
        if (!Number.isFinite(reference) || reference <= 0
                || !Number.isFinite(pixelsPerUnitValue)
                || pixelsPerUnitValue <= 0
                || !Number.isFinite(minimumPixels) || minimumPixels <= 0)
            return 1
        return reference * oneTwoFiveCeiling(
                    minimumPixels / (reference * pixelsPerUnitValue))
    }
}
