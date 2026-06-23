package com.rusefi;

/**
 * 1/22/15
 */
public class TypesHelper {
    private static final String INT8_T = "int8_t";
    public static final String UINT8_T = "uint8_t";
    public static final String UINT_16_T = "uint16_t";
    public static final String INT_16_T = "int16_t";
    private static final String FLOAT_T = "float";
    private static final String INT_32_T = "int";
    private static final String UINT_32_T = "uint32_t";
    private static final String BOOLEAN_T = "boolean";

    public static boolean isPrimitive(String type) {
        return isPrimitive1byte(type) || isPrimitive2byte(type) || isPrimitive4byte(type);
    }

    private static boolean isPrimitive1byte(String type) {
        return type.equals(INT8_T) || type.equals(UINT8_T);
    }

    private static boolean isPrimitive2byte(String type) {
        return type.equals(INT_16_T)
            || type.equals(UINT_16_T);
    }

    private static boolean isPrimitive4byte(String type) {
        return type.equals(INT_32_T) || type.equals(UINT_32_T)
                || isFloat(type);
    }

    public static boolean isBoolean(String type) {
        return BOOLEAN_T.equals(type);
    }

    public static boolean isFloat(String type) {
        return FLOAT_T.equals(type) ||
                // todo: something smarter with dynamic type definition?
                type.equalsIgnoreCase("floatms_t") ||
                type.equalsIgnoreCase("percent_t") ||
                type.equalsIgnoreCase("angle_t");
    }
}
