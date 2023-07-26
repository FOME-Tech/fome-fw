package com.rusefi.newparse;

import com.rusefi.EnumsReader;
import com.rusefi.VariableRegistry;
import com.rusefi.enum_reader.Value;
import com.rusefi.generated.RusefiConfigGrammarBaseListener;
import com.rusefi.generated.RusefiConfigGrammarParser;
import com.rusefi.newparse.parsing.*;
import org.antlr.v4.runtime.tree.ParseTreeListener;

import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static com.rusefi.VariableRegistry.AUTO_ENUM_SUFFIX;

public class ParseState implements DefinitionsState {
    private final Map<String, Definition> definitions = new HashMap<>();
    private final Map<String, Struct> structs = new HashMap<>();
    private final List<Struct> structList = new ArrayList<>();
    private final Map<String, Typedef> typedefs = new HashMap<>();
    private static final Pattern CHAR_LITERAL = Pattern.compile("'.'");
    private static final Pattern AT_SIGN_REPLACEMENT = Pattern.compile("\\@\\@([A-Za-z0-9_]+)\\@\\@");

    private final EnumsReader enumsReader;
    private Definition.OverwritePolicy definitionOverwritePolicy = Definition.OverwritePolicy.NotAllowed;

    private String typedefName = null;
    private final Queue<Double> evalResults = new LinkedList<>();
    private Scope scope = null;
    private final Stack<Scope> scopes = new Stack<>();

    private Struct lastStruct = null;

    public ParseState() {
        this.enumsReader = null;
    }

    public ParseState(EnumsReader enumsReader) {
        this.enumsReader = enumsReader;

        for (Map.Entry<String, EnumsReader.EnumState> enumType : this.enumsReader.getEnums().entrySet()) {
            String name = enumType.getKey();

            for (Value enumValue : enumType.getValue().values()) {
                try {
                    int value = enumValue.getIntValue();

                    this.handleIntDefinition(name + "_" + enumValue.getName(), value);
                } catch (Exception exc) {
                    // ignore parse failures
                }
            }
        }
    }

    private void handleIntDefinition(String name, int value) {
        addDefinition(name, value);

        // Also add ints as 16b hex
        addDefinition(name + "_16_hex", String.format("\\x%02x\\x%02x", (value >> 8) & 0xFF, value & 0xFF));
    }

    private static boolean isNumeric(String str) {
        try {
            Integer.parseInt(str);
            return true;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    public Struct getLastStruct() {
        return lastStruct;
    }

    private String[] resolveEnumValues(String enumName) {
        if (this.enumsReader == null) {
            return new String[0];
        }

        TreeMap<Integer, String> valueNameById = new TreeMap<>();

        EnumsReader.EnumState stringValueMap = this.enumsReader.getEnums().get(enumName);
        if (stringValueMap == null)
            return null;
        for (Value value : stringValueMap.values()) {
            if (isNumeric(value.getValue())) {
                valueNameById.put(value.getIntValue(), value.getName());
            } else {
                Definition def = this.definitions.get(value.getValue());
                if (def == null)
                    throw new IllegalStateException("No value for " + value);
                valueNameById.put((Integer)def.value, value.getName());
            }
        }

        // Now iterate over all values, assembling as an array in-order
        String[] result = new String[valueNameById.lastKey() + 1];
        for (int i = 0; i < result.length; i++) {
            result[i] = valueNameById.getOrDefault(i, "INVALID");
        }

        return result;
    }

    public List<Struct> getStructs() {
        return structList;
    }

    public Definition findDefinition(String name) {
        return definitions.getOrDefault(name, null);
    }

    /**
     * we are in a lengthy period of transition between two implementations
     */
    @Override
    public void addDefinition(VariableRegistry variableRegistry, String name, String value, Definition.OverwritePolicy overwritePolicy) {
        // old implementation
        variableRegistry.register(name, value);
        // new implementation
        addDefinition(name, value, overwritePolicy);
    }

    public void addDefinition(String name, Object value, Definition.OverwritePolicy overwritePolicy) {
        Definition existingDefinition = definitions.getOrDefault(name, null);

        if (existingDefinition != null) {
            switch (existingDefinition.overwritePolicy) {
                case NotAllowed:
                    throw new IllegalStateException("Tried to add definition for " + name + ", but one already existed.");
                case Replace:
                    definitions.remove(name);
                    break;
                case IgnoreNew:
                    // ignore the new definition, do nothing
                    return;
            }
        }

        definitions.put(name, new Definition(name, value, overwritePolicy));
    }

    private void addDefinition(String name, Object value) {
        addDefinition(name, value, definitionOverwritePolicy);
    }

    @Override
    public void setDefinitionPolicy(Definition.OverwritePolicy policy) {
        this.definitionOverwritePolicy = policy;
    }

    public ParseTreeListener getListener() {
        return new RusefiConfigGrammarBaseListener() {

    @Override
    public void exitContent(RusefiConfigGrammarParser.ContentContext ctx) {
        assert(scopes.empty());
        assert(scope == null);
        assert(typedefName == null);
        assert(evalResults.isEmpty());
        assert(evalStack.empty());
    }

    @Override
    public void exitDefinition(RusefiConfigGrammarParser.DefinitionContext ctx) {
        String name = ctx.identifier().getText();

        if (ctx.integer() != null) {
            handleIntDefinition(name, Integer.parseInt(ctx.integer().getText()));
        } else if (ctx.floatNum() != null) {
            addDefinition(name, Double.parseDouble(ctx.floatNum().getText()));
        } else if (ctx.numexpr() != null) {
            double evalResult = evalResults.remove();
            int floored = (int)Math.floor(evalResult);

            if (Math.abs(floored - evalResult) < 0.001) {
                // value is an int, process as such
                handleIntDefinition(name, floored);
            } else {
                // Value is a double, add it
                addDefinition(name, evalResult);
            }
        } else {
            // glue the list of definitions back together
            String defText = ctx.restOfLine().getText();
            addDefinition(name, defText);

            // If the definition matches a char literal, add a special definition for that
            if (CHAR_LITERAL.matcher(defText).find()) {
                addDefinition(name + "_char", defText.charAt(1));
            }
        }
    }

    @Override
    public void enterTypedef(RusefiConfigGrammarParser.TypedefContext ctx) {
        typedefName = ctx.identifier().getText();
    }

    @Override
    public void exitTypedef(RusefiConfigGrammarParser.TypedefContext ctx) {
        typedefName = null;
    }

    @Override
    public void exitScalarTypedefSuffix(RusefiConfigGrammarParser.ScalarTypedefSuffixContext ctx) {
        Type datatype = Type.findByTsType(ctx.Datatype().getText());

        FieldOptions options = new FieldOptions();
        handleFieldOptionsList(options, ctx.fieldOptionsList());

        typedefs.put(typedefName, new ScalarTypedef(typedefName, datatype, options));
    }

    @Override
    public void enterEnumTypedefSuffix(RusefiConfigGrammarParser.EnumTypedefSuffixContext ctx) {
        int endBit = Integer.parseInt(ctx.integer(1).getText());

        Type datatype = Type.findByTsType(ctx.Datatype().getText());

        String rhs = ctx.enumRhs().getText();

        String[] values = null;

        if (rhs.startsWith("@@")) {
            String defName = rhs.replaceAll("@", "");

            if (defName.endsWith(AUTO_ENUM_SUFFIX)) {
                // clip off the "_auto_enum" part
                defName = defName.substring(0, defName.length() - 10);
                values = resolveEnumValues(defName);
            } else {
                Definition def = definitions.get(defName);

                if (def == null) {
                    throw new RuntimeException("couldn't find definition for " + rhs);
                }

                Object value = def.value;

                if (!(value instanceof String)) {
                    throw new RuntimeException("Found definition for " + rhs + " but it wasn't a string as expected");
                }

                rhs = (String)value;
            }
        }

        if (values == null) {
            values = Arrays.stream(rhs.split(","))                    // Split on commas
                        .map(String::trim)                                  // trim whitespace
                        .map(s -> s.replaceAll("\"", ""))   // Remove quotes
                        .toArray(String[]::new);                            // Convert back to array
        }

        typedefs.put(typedefName, new EnumTypedef(typedefName, datatype, endBit, values));
    }

    @Override
    public void exitStringTypedefSuffix(RusefiConfigGrammarParser.StringTypedefSuffixContext ctx) {
        Double stringLength = ParseState.this.evalResults.remove();

        ParseState.this.typedefs.put(ParseState.this.typedefName, new StringTypedef(ParseState.this.typedefName, stringLength.intValue()));
    }

    @Override
    public void enterStruct(RusefiConfigGrammarParser.StructContext ctx) {
        // If we're already inside a struct, push that context on to the stack
        if (scope != null) {
            scopes.push(scope);
        }

        // Create new scratch space for this scope
        scope = new Scope();
    }

    @Override
    public void enterUnionField(RusefiConfigGrammarParser.UnionFieldContext ctx) {
        // Unions behave like a struct as far as scope is concerned (but is processed differently later
        // to overlap all members, instead of placing them in sequence as in a struct)
        enterStruct(null);
    }

    void handleFieldOptionsList(FieldOptions options, RusefiConfigGrammarParser.FieldOptionsListContext ctx) {
        // Null means no options were configured, use defaults
        if (ctx == null) {
            return;
        }

        if (ctx.fieldOption().size() == 0) {
            if (ctx.SemicolonedString() != null) {
                String text = ctx.SemicolonedString().getText();
                options.comment = text.substring(1, text.length() - 1).trim();
            } else if (ctx.SemicolonedSuffix() != null) {
                options.comment = ctx.SemicolonedSuffix().getText().substring(1).trim();
            } else {
                options.comment = "";
            }

            options.comment = processComment(options.comment);

            // this is a legacy field option list, parse it as such
            if (!ctx.numexpr().isEmpty()) {
                options.units = ctx.QuotedString().getText();
                options.scale = evalResults.remove();
                options.offset = evalResults.remove();
                options.min = evalResults.remove();
                options.max = evalResults.remove();
                options.digits = Integer.parseInt(ctx.integer().getText());

                // we should have consumed everything on the results list
                assert(evalResults.size() == 0);
            }

            return;
        }

        for (RusefiConfigGrammarParser.FieldOptionContext fo : ctx.fieldOption()) {
            String key = fo.getChild(0).getText();

            String sValue = fo.getChild(2).getText();

            switch (key) {
                case "unit":
                    options.units = sValue;
                    break;
                case "comment":
                    options.comment = processComment(sValue);
                    break;
                case "digits":
                    options.digits = Integer.parseInt(sValue);
                    break;
                default:
                    Double value = evalResults.remove();

                    switch (key) {
                        case "min":
                            options.min = value;
                            break;
                        case "max":
                            options.max = value;
                            break;
                        case "scale":
                            options.scale = value;
                            break;
                        case "offset":
                            options.offset = value;
                            break;
                    }
                    break;
            }
        }

        // we should have consumed everything on the results list
        assert(evalResults.size() == 0);
    }

    @Override
    public void exitScalarField(RusefiConfigGrammarParser.ScalarFieldContext ctx) {
        String type = ctx.identifier(0).getText();
        String name = ctx.identifier(1).getText();
        boolean autoscale = ctx.Autoscale() != null;

        // First check if this is an instance of a struct
        if (structs.containsKey(type)) {
            scope.structFields.add(new StructField(structs.get(type), name));
            return;
        }

        // Check first if we have a typedef for this type
        Typedef typedef = typedefs.get(type);

        FieldOptions options = null;
        if (typedef != null) {
            if (typedef instanceof ScalarTypedef) {
                ScalarTypedef scTypedef = (ScalarTypedef)typedef;
                // Copy the typedef's options list - we don't want to edit it
                options = scTypedef.options.copy();
                // Switch to the "real" type, that is the typedef's type
                type = scTypedef.type.cType;
            } else if (typedef instanceof EnumTypedef) {
                EnumTypedef bTypedef = (EnumTypedef) typedef;

                options = new FieldOptions();

                // Merge the read-in options list with the default from the typedef (if exists)
                handleFieldOptionsList(options, ctx.fieldOptionsList());

                scope.structFields.add(new EnumField(bTypedef.type, type, name, bTypedef.endBit, bTypedef.values, options));
                return;
            } else if (typedef instanceof StringTypedef) {
                options = new FieldOptions();
                handleFieldOptionsList(options, ctx.fieldOptionsList());

                StringTypedef sTypedef = (StringTypedef) typedef;
                scope.structFields.add(new StringField(name, sTypedef.size, options.comment));
                return;
            } else {
                // TODO: throw
            }
        } else {
            // If no typedef found, it MUST be a native type
            if (!Type.findByCtype(type).isPresent()) {
                throw new RuntimeException("didn't understand type " + type + " for element " + name);
            }

            // no typedef found, create new options list
            options = new FieldOptions();
        }

        // Merge the read-in options list with the default from the typedef (if exists)
        handleFieldOptionsList(options, ctx.fieldOptionsList());

        scope.structFields.add(new ScalarField(Type.findByCtype(type).get(), name, options, autoscale));
    }

    @Override
    public void enterBitField(RusefiConfigGrammarParser.BitFieldContext ctx) {
        String name = ctx.identifier().getText();

        // Check if there's already a bit group at the end of the current struct
        BitGroup group = null;
        if (!scope.structFields.isEmpty()) {
            Object lastElement = scope.structFields.get(scope.structFields.size() - 1);

            if (lastElement instanceof BitGroup) {
                group = (BitGroup)lastElement;

                // If this group is full, create a new one instead of continuing on here.
                if (group.bitFields.size() == 32) {
                    group = null;
                }
            }
        }

        // there was no group, create and add it
        if (group == null) {
            group = new BitGroup();
            scope.structFields.add(group);
        }

        String comment = ctx.SemicolonedSuffix() == null ? null : ctx.SemicolonedSuffix().getText().substring(1).trim();

        comment = processComment(comment);

        String trueValue = "\"true\"";
        String falseValue = "\"false\"";

        if (!ctx.QuotedString().isEmpty()) {
            trueValue = ctx.QuotedString(0).getText();
            falseValue = ctx.QuotedString(1).getText();
        }

        group.addBitField(new BitField(name, comment, trueValue, falseValue));
    }

    private String processComment(String comment) {
        if (comment != null && !comment.isEmpty()) {
            // Try and perform definition replacement on the comment - this is used for output channel
            // human readable names.
            final Matcher matcher = AT_SIGN_REPLACEMENT.matcher(comment);

            if (matcher.find()) {
                String defName = matcher.group(1);

                if (!definitions.containsKey(defName)) {
                    throw new RuntimeException("Definition not found for " + defName);
                }

                comment = definitions.get(defName).value.toString();
            }

            // De-quote any quoted comment
            if (comment.startsWith("\"") && comment.endsWith("\"")) {
                comment = comment.substring(1, comment.length() - 1);
            }
        }

        return comment;
    }

    @Override
    public void exitArrayField(RusefiConfigGrammarParser.ArrayFieldContext ctx) {
        String type = ctx.identifier(0).getText();
        String name = ctx.identifier(1).getText();
        int[] length = this.arrayDim;
        // check if the iterate token is present
        boolean iterate = ctx.Iterate() != null;
        boolean autoscale = ctx.Autoscale() != null;

        if (iterate && length.length != 1) {
            throw new IllegalStateException("Cannot iterate multi dimensional array: " + name);
        }

        // First check if this is an array of structs
        if (structs.containsKey(type)) {
            // iterate required for structs
            assert(iterate);

            scope.structFields.add(new ArrayField<>(new StructField(structs.get(type), name), length, iterate));
            return;
        }

        // Check first if we have a typedef for this type
        Typedef typedef = typedefs.get(type);

        FieldOptions options;
        if (typedef != null) {
            if (typedef instanceof ScalarTypedef) {
                ScalarTypedef scTypedef = (ScalarTypedef) typedef;
                // Copy the typedef's options list - we don't want to edit it
                options = scTypedef.options.copy();
                // Switch to the "real" type, that is the typedef's type
                type = scTypedef.type.cType;
            } else if (typedef instanceof EnumTypedef) {
                EnumTypedef bTypedef = (EnumTypedef) typedef;

                options = new FieldOptions();
                handleFieldOptionsList(options, ctx.fieldOptionsList());

                EnumField prototype = new EnumField(bTypedef.type, type, name, bTypedef.endBit, bTypedef.values, options);

                scope.structFields.add(new ArrayField<>(prototype, length, iterate));
                return;
            } else if (typedef instanceof StringTypedef) {
                StringTypedef sTypedef = (StringTypedef) typedef;

                // iterate required for strings
                assert(iterate);

                options = new FieldOptions();
                handleFieldOptionsList(options, ctx.fieldOptionsList());

                StringField prototype = new StringField(name, sTypedef.size, options.comment);
                scope.structFields.add(new ArrayField<>(prototype, length, iterate));
                return;
            } else {
                throw new RuntimeException("didn't understand type " + type + " for element " + name);
            }
        } else {
            // If no typedef found, it MUST be a native type
            if (!Type.findByCtype(type).isPresent()) {
                throw new RuntimeException("didn't understand type " + type + " for element " + name);
            }

            // no typedef found, create new options list
            options = new FieldOptions();
        }

        // Merge the read-in options list with the default from the typedef (if exists)
        handleFieldOptionsList(options, ctx.fieldOptionsList());

        ScalarField prototype = new ScalarField(Type.findByCtype(type).get(), name, options, autoscale);

        scope.structFields.add(new ArrayField<>(prototype, length, iterate));
    }

    private int[] arrayDim = null;

    @Override
    public void exitArrayLengthSpec(RusefiConfigGrammarParser.ArrayLengthSpecContext ctx) {
        int arrayDim0 = evalResults.remove().intValue();

        if (ctx.ArrayDimensionSeparator() != null) {
            this.arrayDim = new int[] { arrayDim0, evalResults.remove().intValue() };
        } else {
            this.arrayDim = new int[] { arrayDim0 };
        }
    }

    @Override
    public void enterUnusedField(RusefiConfigGrammarParser.UnusedFieldContext ctx) {
        scope.structFields.add(new UnusedField(Integer.parseInt(ctx.integer().getText())));
    }

    @Override
    public void exitStruct(RusefiConfigGrammarParser.StructContext ctx) {
        String structName = ctx.identifier().getText();

        assert(scope != null);

        String comment = ctx.restOfLine() == null ? null : ctx.restOfLine().getText();

        Struct s = new Struct(structName, scope.structFields, ctx.StructNoPrefix() != null, comment);
        structs.put(structName, s);
        structList.add(s);
        lastStruct = s;

        // We're leaving with this struct, re-apply the next struct out so more fields can be added to it
        if (scopes.empty()) {
            scope = null;
        } else {
            scope = scopes.pop();
        }
    }

    @Override
    public void exitUnionField(RusefiConfigGrammarParser.UnionFieldContext ctx) {
        assert(scope != null);

        // unions must have at least 1 member
        assert(!scope.structFields.isEmpty());

        Union u = new Union(scope.structFields);

        // Restore the containing scope
        scope = scopes.pop();

        // Lastly, add the union to the scope
        scope.structFields.add(u);
    }

    private final Stack<Double> evalStack = new Stack<>();

    @Override
    public void exitEvalNumber(RusefiConfigGrammarParser.EvalNumberContext ctx) {
        evalStack.push(Double.parseDouble(ctx.floatNum().getText()));
    }

    @Override
    public void exitEvalReplacement(RusefiConfigGrammarParser.EvalReplacementContext ctx) {
        // Strip any @@ symbols
        String defName = ctx.getText().replaceAll("@", "");

        if (!definitions.containsKey(defName)) {
            throw new RuntimeException("Definition not found for " + ctx.getText());
        }

        // Find the matching definition and push on to the eval stack
        Definition def = definitions.get(defName);

        if (!def.isNumeric()) {
            throw new RuntimeException("Tried to use symbol " + defName + " in an expression, but it wasn't a number");
        }

        evalStack.push(def.asDouble());
    }

    @Override
    public void exitEvalMul(RusefiConfigGrammarParser.EvalMulContext ctx) {
        Double right = evalStack.pop();
        Double left = evalStack.pop();

        evalStack.push(left * right);
    }

    @Override
    public void exitEvalDiv(RusefiConfigGrammarParser.EvalDivContext ctx) {
        Double right = evalStack.pop();
        Double left = evalStack.pop();

        evalStack.push(left / right);
    }

    @Override
    public void exitEvalAdd(RusefiConfigGrammarParser.EvalAddContext ctx) {
        Double right = evalStack.pop();
        Double left = evalStack.pop();

        evalStack.push(left + right);
    }

    @Override
    public void exitEvalSub(RusefiConfigGrammarParser.EvalSubContext ctx) {
        Double right = evalStack.pop();
        Double left = evalStack.pop();

        evalStack.push(left - right);
    }

    @Override
    public void exitNumexpr(RusefiConfigGrammarParser.NumexprContext ctx) {
        assert(evalStack.size() == 1);
        evalResults.add(evalStack.pop());
    }

        };
    }

    static class Scope {
        public final List<Field> structFields = new ArrayList<>();
    }
}
