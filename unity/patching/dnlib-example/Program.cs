// Patches Usagi Yojimbo's Assembly-UnityScript.dll for the handheld port:
//  * the two mouse-only exits also work with keys: Space, Enter or joystick button 0 (A)
//  * the default cInput bindings lose their gamepad alternates (gptokeyb provides keyboard keys)
//   Level9Controller.Update: click on "WaterfallArrow" -> also key press while the exit is active
//   MansionRoom.Update ("Mansion Boss"): hover over "ExitArrow" -> also key press
// Usage: usagipatch <in Assembly-UnityScript.dll> <out dll>
using System;
using System.Linq;
using dnlib.DotNet;
using dnlib.DotNet.Emit;
using dnlib.DotNet.Writer;

var mod = ModuleDefMD.Load(args[0]);
if (mod.Find("UsagiPortPatched", false) != null) { Console.WriteLine("already patched"); return 0; }

// Input.GetKeyDown(KeyCode), taken from the module's existing references
var getKeyDown = mod.GetMemberRefs().First(r => r.Name == "GetKeyDown" && r.DeclaringType.FullName == "UnityEngine.Input"
    && r.MethodSig.Params.Count == 1 && r.MethodSig.Params[0].FullName == "UnityEngine.KeyCode");
int[] keys = { 32 /* Space */, 13 /* Return */, 330 /* JoystickButton0 */ };

// emit: if any key pressed -> goto go, else -> goto orig
Instruction[] KeyCheck(Instruction go, Instruction orig) {
    var list = keys.SelectMany(k => new[] { Instruction.Create(OpCodes.Ldc_I4, k), Instruction.Create(OpCodes.Call, getKeyDown),
                                            Instruction.Create(OpCodes.Brtrue, go) }).ToList();
    list.Add(Instruction.Create(OpCodes.Br, orig));
    return list.ToArray();
}

void InsertAt(CilBody body, Instruction before, Instruction[] code) {
    int idx = body.Instructions.IndexOf(before);
    foreach (var ins in code.Reverse()) body.Instructions.Insert(idx, ins);
}

// ---- Level9Controller.Update: insert after the checkInput test, before GetMouseButtonDown
{
    var m = mod.Find("Level9Controller", false).FindMethod("Update");
    var ins = m.Body.Instructions;
    var mouseCall = ins.First(i => i.OpCode == OpCodes.Call && i.Operand is IMethod mm && mm.Name == "GetMouseButtonDown");
    var orig = ins[ins.IndexOf(mouseCall) - 1];                    // the ldc.i4.0 argument of GetMouseButtonDown
    var checkInput = (IField)ins[1].Operand;
    var fade = (IMethod)ins.First(i => i.Operand is IMethod mm && mm.Name == "FadeAndLoadLevel").Operand;
    var startCo = (IMethod)ins.First(i => i.Operand is IMethod mm && mm.Name == "StartCoroutine_Auto").Operand;
    var go = Instruction.Create(OpCodes.Ldarg_0);
    var action = new[] { go, Instruction.Create(OpCodes.Ldc_I4_0), Instruction.Create(OpCodes.Stfld, checkInput),
        Instruction.Create(OpCodes.Ldarg_0), Instruction.Create(OpCodes.Ldarg_0), Instruction.Create(OpCodes.Ldstr, "WaterfallArrow"),
        Instruction.Create(OpCodes.Callvirt, fade), Instruction.Create(OpCodes.Callvirt, startCo), Instruction.Create(OpCodes.Pop),
        Instruction.Create(OpCodes.Ret) };
    InsertAt(m.Body, orig, KeyCheck(go, orig).Concat(action).ToArray());
    Console.WriteLine("Level9Controller.Update patched");
}

// ---- MansionRoom.Update: inside the "Mansion Boss" branch, before the mouse raycast
{
    var m = mod.Find("MansionRoom", false).FindMethod("Update");
    var ins = m.Body.Instructions;
    int bossIdx = ins.IndexOf(ins.First(i => i.OpCode == OpCodes.Ldstr && (string)i.Operand == "Mansion Boss"));
    var orig = ins[bossIdx + 3];                                    // first instruction of the boss branch
    var mcp = (IField)ins.First(i => i.OpCode == OpCodes.Ldfld && ((IField)i.Operand).Name == "_MCP").Operand;
    var setLevel = (IMethod)ins.First(i => i.Operand is IMethod mm && mm.Name == "SetLevelToLoadByIndex").Operand;
    var loadLevel = (IMethod)ins.First(i => i.Operand is IMethod mm && mm.Name == "LoadLevel").Operand;
    var go = Instruction.Create(OpCodes.Ldarg_0);
    var action = new[] { go, Instruction.Create(OpCodes.Ldfld, mcp), Instruction.Create(OpCodes.Ldc_I4_3),
        Instruction.Create(OpCodes.Callvirt, setLevel), Instruction.Create(OpCodes.Ldstr, "Loading"),
        Instruction.Create(OpCodes.Call, loadLevel), Instruction.Create(OpCodes.Ret) };
    InsertAt(m.Body, orig, KeyCheck(go, orig).Concat(action).ToArray());
    Console.WriteLine("MansionRoom.Update patched");
}

// ---- MCP: drop the gamepad alternates from the default cInput bindings. On the handheld the
// buttons reach the game as keyboard keys through gptokeyb; Unity would also read the raw pad and
// fire a second, different action (raw button 0 is Light Attack while gptokeyb sends Jump).
{
    int n = 0;
    foreach (var m in mod.Find("MCP", false).Methods.Where(mm => mm.HasBody))
        foreach (var i in m.Body.Instructions)
            if (i.OpCode == OpCodes.Ldstr && i.Operand is string str && (str.StartsWith("Joystick1Button") || str.StartsWith("Joy1 Axis")))
            { i.Operand = "None"; n++; }
    Console.WriteLine($"MCP: {n} gamepad bindings removed");
}

// marker type so a second run is a no-op
mod.Types.Add(new TypeDefUser("", "UsagiPortPatched", mod.CorLibTypes.Object.TypeDefOrRef));
foreach (var t in new[] { "Level9Controller", "MansionRoom" }) {
    var b = mod.Find(t, false).FindMethod("Update").Body;
    b.SimplifyBranches(); b.OptimizeBranches();
}
var opts = new ModuleWriterOptions(mod) { MetadataOptions = { Flags = MetadataFlags.PreserveAll | MetadataFlags.KeepOldMaxStack } };
mod.Write(args[1], opts);
Console.WriteLine($"written {args[1]}");
return 0;
