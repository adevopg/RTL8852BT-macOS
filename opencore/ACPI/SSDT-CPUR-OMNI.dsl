/*
 * SSDT-CPUR-OMNI - objetos Processor para macOS en el HP OmniBook (Ryzen AI 7 350)
 *
 * El firmware declara los 16 hilos como Device (ACPI0007) en \_SB.PLTF.C000..C00F.
 * macOS solo reconoce objetos Processor, asi que aqui se declaran 16, uno por hilo,
 * con el mismo ProcId y _UID que la tabla MADT del firmware (0..15, APIC ID 0..15).
 *
 * _STA devuelve 0 fuera de macOS: Windows no los ve.
 * Sin _DSM plugin-type: eso es para la gestion de energia de Intel (XCPM).
 * Mismo enfoque que SSDT-CPUR (placas AMD B550/A520) y SSDT-PLUG-ALT de OpenCore.
 *
 * Compilar: iasl SSDT-CPUR-OMNI.dsl   (16 avisos "Legacy Processor() keyword": esperados)
 */
DefinitionBlock ("", "SSDT", 2, "ACDT", "CPUR", 0x00001000)
{
    Scope (\_SB)
    {
        Processor (CP00, 0x00, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP01, 0x01, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP02, 0x02, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP03, 0x03, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP04, 0x04, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP05, 0x05, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP06, 0x06, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP07, 0x07, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP08, 0x08, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 8)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP09, 0x09, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 9)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0A, 0x0A, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 10)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0B, 0x0B, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 11)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0C, 0x0C, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 12)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0D, 0x0D, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 13)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0E, 0x0E, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 14)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }

        Processor (CP0F, 0x0F, 0x00000810, 0x06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 15)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Else
                {
                    Return (Zero)
                }
            }
        }
    }
}
