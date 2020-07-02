DefinitionBlock ("", "SSDT", 2, "hack", "fnkey", 0x00000000)
{
    // remap Fn+F5
    Name (_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "Custom PS2 Map", Package()
            {
                Package () {},
                "76=64",  // Fn+F5=F13
            },
        },
    })
}
