/* Paramix Studio UI. Everything stays in this browser tab: the ROM is read with
 * FileReader and the new ROM is offered as a download. Author: mameise */
(function () {
'use strict';
const C = window.ParamixCore;
const E = window.ParamixEmu;
const D = window.ParamixDoom;
const F = window.ParamixFeld;
const S6 = window.ParamixSix;
const $ = (id) => document.getElementById(id);

// ------------------------------------------------------------------ texts
const T = {
  de: {
    open: 'ROM öffnen', save: 'Neues ROM speichern',
    startTitle: 'Eigene Figuren für dein Paradise',
    startText: 'Lade ein Paradise-Abbild (16 MB Flash-Dump). Du bekommst jede Figur als Bild-Sheet, bearbeitest sie in deinem Malprogramm und lädst sie wieder hoch. Am Ende speicherst du ein neues ROM.',
    dropTitle: 'ROM hierher ziehen oder klicken',
    dropText: 'Land, Water, Sky, Jade Forest, Tropical und Glacier werden erkannt.',
    f1t: 'Bleibt bei dir', f1: 'Das ROM wird nirgends hochgeladen. Alles läuft in diesem Browserfenster, auch ohne Internet.',
    f2t: 'Sheets mit festem Raster', f2: 'Jede Zelle hat ihre feste Stelle. Durchsichtig bleibt durchsichtig, Farben werden automatisch auf die Palette gebracht.',
    f3t: 'Prüfung beim Speichern', f3: 'Prüfsummen werden neu gerechnet und jedes geänderte Bild wird vor dem Speichern noch einmal gelesen.',
    planTitle: 'Dein neues ROM', planFig: 'Figuren', planEmu: 'Emulator', planDoom: 'DOOM', planMenu: 'Menü',
    stNone: 'nicht dabei', stKeep: 'bleibt', stNew: 'neu', stDel: 'wird entfernt', stChanged: '{n} geändert', stSame: 'unverändert',
    segFw: 'Firmware und Figuren', segEmu: 'Emulator', segDoom: 'DOOM', segSlots: 'Spielstände der Felder', segSave: 'Spielstand', segFree: 'frei',
    ruleBoth: 'Emulator oder DOOM: es passt nur eines von beiden ins ROM. Das Menü passt zu beidem.',
    ruleNoRoom: 'In dieser Ausgabe ist kein Platz für Emulator und DOOM (die Figuren-Daten sind grösser). Das Menü geht.',
    emuVsDoom: 'Achtung: DOOM ist gewählt oder eingebaut. Mit einer Firmware hier fällt DOOM weg.',
    doomVsEmu: 'Achtung: ein Emulator ist gewählt oder eingebaut. Mit einer WAD hier fällt der Emulator weg.',
    noRoomApp: 'In dieser Ausgabe ist kein Platz dafür.',
    doomHas: 'In diesem ROM ist DOOM eingebaut.', doomOff: 'DOOM entfernen', doomGone: 'DOOM wird beim Speichern entfernt.',
    keysTitle: 'Tasten', kFire: 'Schiessen', kFwd: 'Vorwärts', kBack: 'Zurück', kUse: 'Benutzen', kMod: 'Waffe wechseln: halten + Rad',
    kWheel: 'Rad drücken', kRun: 'Laufen mit 2x Vorwärts', kInv: 'Rad andersherum', kFixed: 'Fest: A+C Menü, B+C Karte, Rad 2 s zurück ins Paradise, Haltetaste + Rad drücken Ton.',
    kOld: 'Dieses DOOM ist älter und kennt noch keine eigene Tastenbelegung. Mit der WAD neu einbauen.',
    device: 'Gerät', devTitle: 'Gerät über UART', devHint: 'UART-Dongle mit 3,3 V an die drei Kontakte: links RX vom Dongle, Mitte GND, rechts TX vom Dongle. Am Paradise den Download-Bildschirm öffnen (Planet, A, Connect, Download, Download), dann Verbinden. Braucht Chrome oder Edge am Computer.',
    devConnect: 'Verbinden', devDump: 'ROM auslesen', devWrite: 'Neues ROM aufs Gerät schreiben', devOnly: 'Nur Änderungen schreiben', devKeep: 'Spielstände behalten', devStop: 'Abbrechen',
    devNoSerial: 'Dieser Browser kann nicht mit dem Dongle reden. Bitte Chrome oder Edge am Computer verwenden.', devNone: 'Nicht verbunden.',
    devOn: 'Verbunden: {ed}, {mb} MB{l}', devLoaded: ', Flash-App geladen', devReady: 'Bereit.', devNeedRom: 'Zum Schreiben zuerst ein ROM öffnen (oder auslesen).',
    phload: 'Flash-App laden', phread: 'Auslesen', phcompare: 'Vergleichen', phwrite: 'Schreiben', phcheck: 'Prüfen', phprepare: 'Vorbereiten',
    devDumped: 'ROM ausgelesen und als {n} gespeichert. Es ist jetzt im Studio geöffnet.',
    devSure: 'Das neue ROM wird jetzt aufs Gerät geschrieben. Gerät während des Schreibens eingeschaltet lassen. Auf eigene Gefahr schreiben?',
    devSureFull: 'Ohne "Spielstände behalten" wird auch der Spielstand mit dem aus dem ROM überschrieben. Trotzdem schreiben?',
    devDone: 'Fertig und geprüft: {w} Sektoren geschrieben{k}. Das Gerät kann neu gestartet werden (Reset oder Batterie).', devKept: ', {k} Sektoren Spielstand behalten',
    devSame: 'Nichts zu schreiben, das Gerät hat schon dieses ROM.', devErr: 'Fehler: {e}', devCancelled: 'Abgebrochen.',
    fl_timeout: 'keine Antwort vom Gerät ({c} {a})', fl_noAck: 'das Gerät antwortet nicht. Steht es auf dem Download-Bildschirm?', fl_repeats: 'zu viele Wiederholungen beim Laden',
    fl_devCancel: 'das Gerät hat das Laden abgebrochen', fl_unexpected: 'unerwartete Antwort: {r}', fl_hello: 'unerwartete Antwort auf HELLO', fl_notReady: 'Flash nicht bereit',
    fl_readErr: 'Lesefehler bei {a}', fl_noCrc: 'keine Prüfsumme bei {a}', fl_cancelled: 'abgebrochen', fl_size: 'das Abbild muss genau 16 MB gross sein',
    fl_notParadise: 'kein Paradise-Abbild (SONIXDEV fehlt)', fl_cancelledWrite: 'abgebrochen. Gerät eingeschaltet lassen und nochmals schreiben',
    fl_notAccepted: 'Sektor {a} nicht angenommen. Gerät eingeschaltet lassen und nochmals schreiben', fl_differs: 'Sektor {a} weicht nach dem Schreiben ab. Gerät eingeschaltet lassen und nochmals schreiben',
    fl_checkFail: 'Prüfung bei {a} falsch. Gerät eingeschaltet lassen und nochmals schreiben', fl_noAnswer: 'keine Antwort. Gerät auf den Download-Bildschirm stellen und nochmals versuchen',
    fl_appSilent: 'die Flash-App ist gesendet, antwortet aber nicht. Nochmals versuchen', fl_unknownEd: 'Ausgabe des neuen ROMs unbekannt, "Spielstände behalten" abwählen',
    fl_otherEd: 'das Gerät ist {d}, das neue ROM ist {r}. Zum Wechseln der Ausgabe "Spielstände behalten" abwählen',
    fl_noteNoFit: 'Feld {f} passt nicht ins neue Fach und wird nicht behalten', fl_noteMoved: 'Feld {f} ist ins neue Fach umgezogen',
    fl_noteDropped: 'das neue ROM hat kein Feld-Menü, die gespeicherten Felder auf dem Gerät fallen weg', fl_noteEmu: 'TamaEmu-Firmware neu gewählt, der Emulator startet frisch. Für ein Update das ROM nur öffnen und speichern',
    tabFeld: 'Menü', feldOn: 'Feldwahl', bagOn: 'Items und Tamapoints',
    sixOn: 'Alle 6 Felder', sixPick: 'Land-, Water- oder Sky-ROM wählen', sixHint: 'Holt Land, Water und Sky zu Tropical, Glacier und Forest dazu. Die Feldbilder kommen aus deinem eigenen Land-, Water- oder Sky-ROM.',
    sixNeed: 'Für alle 6 Felder fehlt noch ein Land-, Water- oder Sky-ROM.', sixOk: 'Feldbilder aus {n} übernommen.', sixBad: '{n} ist kein Land-, Water- oder Sky-ROM.', sixIn: 'Alle 6 Felder sind eingebaut.', sixFail: 'Felder nicht eingebaut: {e}', sixNoFw: 'Die Firmware dieses ROMs ist verändert, weitere Felder gehen hier nicht.', planSix: 'Mehr Felder', sixOnJ: 'Sky als 4. Feld', sixHintJ: 'Jade Forest hat die Sky-Bilder schon dabei.', sixInJ: 'Sky ist als 4. Feld eingebaut.', sixOld: 'Alle 6 Felder sind eingebaut, aber in einer älteren Version. Wähl dein Land-, Water- oder Sky-ROM, dann wird beim Speichern alles nachgerüstet.', sixOldJ: 'Sky ist eingebaut und wird beim Speichern auf den neuen Stand gebracht.', sixPlanet: 'Planet passend zum Feld', sixPlanetNeed: 'Zum Ändern des Planeten braucht es nochmals dein Land-, Water- oder Sky-ROM.', sixApp: 'DOOM und Emulator passen nicht dazu',
    feldHint: 'Kleines Menü im ROM: Feld wechseln (jedes mit eigenem Spielstand), Items in die Tasche legen, Tamapoints setzen.',
    feldUse: 'Öffnen: auf dem Heimbildschirm A und C zusammen halten. Rad wählt, B bestätigt, C zurück.',
    feldNo: 'Feldwahl geht mit Land, Water, Sky (untereinander) und Tropical, Glacier (untereinander).', feldMissing: 'In dieser Ausgabe fehlt das Menü.',
    feldCur: 'Aktives Feld: {f}.', feldList: 'Wählbar: {s}.', feldStored: 'Gespeichert: {s}.', feldFail: 'Menü nicht eingebaut: {e}', feldUnknown: 'Ausgabe nicht erkannt.',
    tabFigs: 'Figuren', tabAll: 'Alle Sprites', tabDim: 'Karte', tabEmu: 'Emulator', tabDoom: 'DOOM',
    doomPick: 'DOOM.WAD wählen', doomEp: 'Episode', doomMusic: 'Musik',
    doomHint: 'Baut DOOM ins ROM. Die WAD bringst du selbst mit, sie bleibt auf deinem Computer.',
    doomUse: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad 2 s drücken.',
    doomNone: 'Keine WAD gewählt.', doomOk: 'WAD erkannt ({eps}). DOOM wird beim Speichern eingebaut.',
    doomBad: 'WAD nicht erkannt: {e}', doomFail: 'DOOM nicht eingebaut: {e}', doomMissing: 'In dieser Ausgabe fehlt das DOOM-Programm.',
    doomBoth: 'Emulator und DOOM zusammen passen nicht in ein ROM. Bitte nur eines wählen.',
    doomBig: 'Keine Episode dieser WAD passt ins ROM.', doomNoMusic: 'Diese Episode passt nur ohne Musik.',
    emuPick: 'Firmware wählen', emuOff: 'Emulator entfernen',
    emuHint: 'Baut einen Emulator für Farb-Tamas ins ROM. Die Firmware (4 MB oder 8 MB) bringst du selbst mit.',
    emuUse: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Neu beginnen: A und C loslassen und in den ersten 3 Sekunden das Rad drücken.',
    emuNone: 'Kein Emulator gewählt.', emuHas: 'In diesem ROM ist ein Emulator eingebaut.', emuUp: 'Beim Speichern kommt die neue Emulator-Version hinein. Mit "Spielstände behalten" bleibt der Spielstand.',
    emuOk: 'Firmware erkannt ({size}). Sie wird beim Speichern eingebaut.', emuBad: 'Firmware nicht erkannt: {e}',
    emuGone: 'Emulator wird beim Speichern entfernt.', emuMissing: 'In dieser Ausgabe fehlt das Emulator-Programm.',
    emuFail: 'Emulator nicht eingebaut: {e}', savedEmu: 'Gespeichert: {name}',
    dlcPick: 'Download-Inhalte hinzufügen', dlcHint: 'Essen, Kleider, Accessoires, Spiele usw. als .jpg aus der Tamagotchi-App. Mehrere Dateien auf einmal gehen.', dlcFreeN: '{n} frei', dlcDel: 'entfernen', dlcK0: 'Mahlzeiten', dlcK1: 'Snacks', dlcK2: 'Kleider', dlcK3: 'Accessoires', dlcK4: 'Gegenstände', dlcK5: 'Alltagsdinge', dlcK6: 'Hintergründe', dlcK7: 'Spiele', dlcK8: 'Ausflugsziele', dlcOk: '{n} liegt jetzt in {k}, Platz {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} entfernt.',
    figField: 'Feld:', figAll: 'alle', zipField: 'Feld als ZIP', fieldZipOk: '{f}: {n} Figuren und {s} Bilder als ZIP.', fieldIn: 'Feld aus ZIP übernehmen', fieldInBad: 'Kein Feld-ZIP (field.json fehlt).', fieldInOk: '{src} in {dst} übernommen: {n} Teile.', fieldInEyes: '{k} Figuren bleiben original, ihre Augenbilder sind hier zu gross.',
    zipFigs: 'Alle Figuren als ZIP', zipAll: 'Alle Sprites als ZIP', upload: 'Sheets hochladen (PNG oder ZIP)',
    numbers: 'Nummern-Test-ROM',
    figHint: 'Eine Figur besteht aus Körper, Augen und Mund, je gross und klein. Klick auf eine Figur für ihr Sheet.',
    allHint: 'Hintergründe, Menüs, Gegenstände, Schriften.',
    moreColors: 'Mehr Farben erlauben (bis 256)',
    dimLoad: 'Karte laden', dimHint: 'Abbild einer Figurenkarte (4 MB). Kartenfiguren ersetzen am besten Erwachsene. Baby, Kind und Teen teilen sich die Augen und bleiben original.',
    dimPick: 'Kartenfigur', dimTarget: 'ersetzt Figur', dimApply: 'Einsetzen', dimEgg: 'Ei der Karte einsetzen',
    dimBaby: 'Baby', dimNone: 'Lade eine Karte, um ihre Figuren zu sehen.',
    download: 'Sheet herunterladen', uploadOne: 'Sheet hochladen', reset: 'Original wiederherstellen', close: 'Schliessen',
    working: 'Einen Moment …',
    dimNr: 'Nr.', stUpd: 'wird aktualisiert',
    disclaimer: 'Benutzung auf eigene Gefahr. Das Ändern und Beschreiben des Geräts kann es unbrauchbar machen oder Spielstände löschen. Lies vorher ein Backup aus und nutze das Tool nur, wenn du weisst, was du tust. Es gibt keine Garantie und keine Haftung für Schäden an Geräten oder Daten.',
    trademarks: 'Tamagotchi ist eine Marke von BANDAI, DOOM eine Marke von id Software. Alle Marken gehören ihren jeweiligen Inhabern und werden nur genannt, um zu beschreiben, wofür das Tool gedacht ist. Kein offizielles Produkt, keine Verbindung zu diesen Firmen.',
    devWarn: 'Schreiben auf das Gerät geschieht auf eigene Gefahr und ohne Haftung. Vorher ein Backup auslesen und das Gerät während des Schreibens eingeschaltet lassen.',
    footer: 'Läuft lokal im Browser. Von mameise.',
    edition: 'Edition', sprites: 'Sprites', figures: 'Figuren', changed: 'geändert', space: 'Platz',
    figure: 'Figur', sprite: 'Sprite',
    mSize: 'Zelle {w}×{h}, {n} Bilder, Raster {iw}×{ih}', mColors: '{c}, Palettensätze: {p}',
    mDirect: 'Direktfarbe RGB565', mPal: '{n} Farben', mKeep: 'Mehrere Palettensätze (Farbvarianten): beim Hochladen werden die Originalfarben benutzt.',
    mSheet: 'Sheet {w}×{h} px. Grösse nicht ändern, sonst passt das Raster nicht mehr.',
    mGroupRows: 'Zeilen: Körper (20), Augen (20), Mund (18), Körper klein (24), Augen klein (14), Mund klein (14).',
    notRom: 'Das ist kein Paradise-Abbild: {e}', loaded: '{ed} geladen: {s} Sprites, {f} Figuren.',
    saved: 'Gespeichert: {name}\n{n} Sprites geändert, noch {kb} KB frei.', nothing: 'Noch nichts geändert.',
    tooBig: 'Zu gross für den freien Platz. {e}',
    upOk: 'Übernommen: {n}', upSkip: 'Übersprungen: {list}', upNone: 'Keine passenden Dateien gefunden. Erwartet: figur_0237.png oder sprite_0014.png.',
    wrongSize: '{f}: {e}', resetOk: 'Original wiederhergestellt.', dimOk: 'Kartenfigur {d} sitzt jetzt in Figur {g}.', eggOk: 'Ei ersetzt.',
    numOk: 'Jede Figur zeigt jetzt ihre Nummer. Speichern, aufspielen, aufziehen und notieren, welche Nummer in welcher Stufe kommt.',
    notDim: 'Diese Karte kenne ich nicht: {e}', leave: 'Es gibt ungespeicherte Änderungen.',
    roleFallback: 'Ersatzfigur', roleBaby: 'Baby', roleAdult: 'erster Erwachsener',
    eyeHint: 'Pinker Rahmen = Auge. Es wandert in die Augenebene, damit die Figur blinzelt und die Zelle Augen hat. Falsch? Aufs Auge klicken.',
    eyeNone: 'Figur hat keine sichtbaren Augen', dimEyesTooBig: 'Die Augen waren zu detailreich, die Figur bleibt ohne Augenebene.',
    roleFallbackText: 'erscheint, wenn das Spiel eine Figur nicht laden kann. Am besten unverändert lassen.',
    roleBabyText: 'schlüpft aus dem Ei.', roleAdultText: 'ab hier hat jede Figur eigene Augen.',
    mEyes: 'Augen behalten ihre Bytegrösse, sonst schlüpft die Ersatzfigur. Zu detailreiche Augen werden abgelehnt.',
    mColorsFig: 'Bleiben alle Farben aus dem Original, färbt das Spiel die Figur weiter je nach Tama um. Neue Farben werden fest (bis 8 Farben plus Umriss).',
  },
  en: {
    open: 'Open ROM', save: 'Save new ROM',
    startTitle: 'Your own characters for Paradise',
    startText: 'Load a Paradise dump (16 MB flash image). Every character comes out as an image sheet, you edit it in any paint program and upload it again. Then save a new ROM.',
    dropTitle: 'Drop a ROM here or click',
    dropText: 'Land, Water, Sky, Jade Forest, Tropical and Glacier are recognised.',
    f1t: 'Stays on your computer', f1: 'The ROM is never uploaded. Everything runs in this browser tab, even offline.',
    f2t: 'Sheets with a fixed grid', f2: 'Every cell has a fixed place. Transparent stays transparent, colours are fitted to the palette automatically.',
    f3t: 'Checked on save', f3: 'Checksums are recalculated and every changed image is read back before saving.',
    planTitle: 'Your new ROM', planFig: 'Characters', planEmu: 'Emulator', planDoom: 'DOOM', planMenu: 'Menu',
    stNone: 'not included', stKeep: 'stays', stNew: 'new', stDel: 'will be removed', stChanged: '{n} changed', stSame: 'unchanged',
    segFw: 'Firmware and characters', segEmu: 'Emulator', segDoom: 'DOOM', segSlots: 'Field saves', segSave: 'Save', segFree: 'free',
    ruleBoth: 'Emulator or DOOM: only one of the two fits into the ROM. The menu works with both.',
    ruleNoRoom: 'This edition has no room for emulator and DOOM (its character data is larger). The menu works.',
    emuVsDoom: 'Note: DOOM is chosen or built in. Choosing a firmware here drops DOOM.',
    doomVsEmu: 'Note: an emulator is chosen or built in. Choosing a WAD here drops the emulator.',
    noRoomApp: 'This edition has no room for it.',
    doomHas: 'This ROM contains DOOM.', doomOff: 'Remove DOOM', doomGone: 'DOOM is removed on save.',
    keysTitle: 'Buttons', kFire: 'Fire', kFwd: 'Forward', kBack: 'Back', kUse: 'Use', kMod: 'Change weapon: hold + wheel',
    kWheel: 'Wheel press', kRun: 'Run with 2x forward', kInv: 'Wheel reversed', kFixed: 'Fixed: A+C menu, B+C map, wheel 2 s back to Paradise, hold button + wheel press sound.',
    kOld: 'This DOOM is older and has no own button layout yet. Build it in again with the WAD.',
    device: 'Device', devTitle: 'Device over UART', devHint: 'UART dongle with 3.3 V on the three contacts: left RX from the dongle, middle GND, right TX from the dongle. Open the download screen on the Paradise (Planet, A, Connect, Download, Download), then Connect. Needs Chrome or Edge on a computer.',
    devConnect: 'Connect', devDump: 'Read ROM', devWrite: 'Write new ROM to the device', devOnly: 'Only write changes', devKeep: 'Keep saves', devStop: 'Cancel',
    devNoSerial: 'This browser cannot talk to the dongle. Please use Chrome or Edge on a computer.', devNone: 'Not connected.',
    devOn: 'Connected: {ed}, {mb} MB{l}', devLoaded: ', flash app loaded', devReady: 'Ready.', devNeedRom: 'To write, open a ROM first (or read one).',
    phload: 'Loading flash app', phread: 'Reading', phcompare: 'Comparing', phwrite: 'Writing', phcheck: 'Checking', phprepare: 'Preparing',
    devDumped: 'ROM read and saved as {n}. It is now open in the Studio.',
    devSure: 'The new ROM is written to the device now. Keep the device switched on while writing. Write at your own risk?',
    devSureFull: 'Without "Keep saves" the save on the device is replaced by the one in the ROM. Write anyway?',
    devDone: 'Done and checked: {w} sectors written{k}. Restart the device (reset or battery).', devKept: ', {k} save sectors kept',
    devSame: 'Nothing to write, the device already has this ROM.', devErr: 'Error: {e}', devCancelled: 'Cancelled.',
    tabFeld: 'Menu', feldOn: 'Field switch', bagOn: 'Items and Tamapoints',
    sixOn: 'All 6 fields', sixPick: 'Choose Land, Water or Sky ROM', sixHint: 'Adds Land, Water and Sky to Tropical, Glacier and Forest. The field pictures come from your own Land, Water or Sky ROM.',
    sixNeed: 'All 6 fields still need a Land, Water or Sky ROM.', sixOk: 'Field pictures taken from {n}.', sixBad: '{n} is not a Land, Water or Sky ROM.', sixIn: 'All 6 fields are built in.', sixFail: 'Fields not built in: {e}', sixNoFw: 'The firmware of this ROM is changed, more fields do not work here.', planSix: 'More fields', sixOnJ: 'Sky as 4th field', sixHintJ: 'Jade Forest already has the Sky pictures.', sixInJ: 'Sky is built in as the 4th field.', sixOld: 'All 6 fields are built in, but an older version. Choose your Land, Water or Sky ROM and everything is updated when you save.', sixOldJ: 'Sky is built in and is updated when you save.', sixPlanet: 'Planet to match the field', sixPlanetNeed: 'Changing the planet needs your Land, Water or Sky ROM again.', sixApp: 'DOOM and the emulator do not fit with it',
    feldHint: 'Small menu in the ROM: switch fields (each with its own save), put items in the bag, set Tamapoints.',
    feldUse: 'Open: hold A and C together on the home screen. Wheel selects, B confirms, C goes back.',
    feldNo: 'Field switch works with Land, Water, Sky (among each other) and Tropical, Glacier (among each other).', feldMissing: 'This build does not contain the menu.',
    feldCur: 'Active field: {f}.', feldList: 'Selectable: {s}.', feldStored: 'Saved: {s}.', feldFail: 'Menu not built in: {e}', feldUnknown: 'Edition not recognised.',
    tabFigs: 'Characters', tabAll: 'All sprites', tabDim: 'Card', tabEmu: 'Emulator', tabDoom: 'DOOM',
    doomPick: 'Choose DOOM.WAD', doomEp: 'Episode', doomMusic: 'Music',
    doomHint: 'Builds DOOM into the ROM. Bring your own WAD, it stays on your computer.',
    doomUse: 'Start: hold A and C together on the home screen. Back: hold the wheel for 2 s.',
    doomNone: 'No WAD chosen.', doomOk: 'WAD recognised ({eps}). DOOM is built in on save.',
    doomBad: 'WAD not recognised: {e}', doomFail: 'DOOM not built in: {e}', doomMissing: 'This build does not contain the DOOM program.',
    doomBoth: 'Emulator and DOOM do not fit into one ROM together. Please choose only one.',
    doomBig: 'No episode of this WAD fits into the ROM.', doomNoMusic: 'This episode only fits without music.',
    emuPick: 'Choose firmware', emuOff: 'Remove emulator',
    emuHint: 'Builds an emulator for colour tamas into the ROM. Bring your own firmware (4 MB or 8 MB).',
    emuUse: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). Start over: release A and C and press the wheel within the first 3 seconds.',
    emuNone: 'No emulator chosen.', emuHas: 'This ROM contains an emulator.', emuUp: 'Saving puts in the new emulator version. With "Keep saves" the save stays.',
    emuOk: 'Firmware recognised ({size}). It is built in on save.', emuBad: 'Firmware not recognised: {e}',
    emuGone: 'The emulator is removed on save.', emuMissing: 'This build does not contain the emulator program.',
    emuFail: 'Emulator not built in: {e}', savedEmu: 'Saved: {name}',
    dlcPick: 'Add download content', dlcHint: 'Food, clothes, accessories, games etc. as .jpg from the Tamagotchi app. Several files at once work.', dlcFreeN: '{n} free', dlcDel: 'remove', dlcK0: 'Meals', dlcK1: 'Snacks', dlcK2: 'Clothing', dlcK3: 'Accessories', dlcK4: 'Items', dlcK5: 'Daily items', dlcK6: 'Wallpapers', dlcK7: 'Games', dlcK8: 'Destinations', dlcOk: '{n} now sits in {k}, slot {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} removed.',
    figField: 'Field:', figAll: 'all', zipField: 'Field as ZIP', fieldZipOk: '{f}: {n} characters and {s} pictures as ZIP.', fieldIn: 'Load field from ZIP', fieldInBad: 'Not a field ZIP (field.json missing).', fieldInOk: '{src} applied to {dst}: {n} parts.', fieldInEyes: '{k} characters stay original, their eye images are too big here.',
    zipFigs: 'All characters as ZIP', zipAll: 'All sprites as ZIP', upload: 'Upload sheets (PNG or ZIP)',
    numbers: 'Number test ROM',
    figHint: 'A character is made of body, eyes and mouth, each large and small. Click a character for its sheet.',
    allHint: 'Backgrounds, menus, items, fonts.',
    moreColors: 'Allow more colours (up to 256)',
    dimLoad: 'Load card', dimHint: 'Dump of a character card (4 MB). Card characters best replace adults. Baby, child and teen share their eyes and stay original.',
    dimPick: 'Card character', dimTarget: 'replaces character', dimApply: 'Insert', dimEgg: 'Insert the card’s egg',
    dimBaby: 'Baby', dimNone: 'Load a card to see its characters.',
    download: 'Download sheet', uploadOne: 'Upload sheet', reset: 'Restore original', close: 'Close',
    working: 'One moment …',
    dimNr: 'No.', stUpd: 'gets updated',
    disclaimer: 'Use at your own risk. Changing and writing the device can make it unusable or erase saves. Read a backup first and only use the tool if you know what you are doing. There is no warranty and no liability for damage to devices or data.',
    trademarks: 'Tamagotchi is a trademark of BANDAI, DOOM is a trademark of id Software. All trademarks belong to their owners and are only named to describe what the tool is for. Not an official product, not affiliated with these companies.',
    devWarn: 'Writing to the device is at your own risk and without liability. Read a backup first and keep the device switched on while writing.',
    footer: 'Runs locally in your browser. By mameise.',
    edition: 'Edition', sprites: 'Sprites', figures: 'Characters', changed: 'changed', space: 'Space',
    figure: 'Character', sprite: 'Sprite',
    mSize: 'Cell {w}×{h}, {n} images, grid {iw}×{ih}', mColors: '{c}, palette sets: {p}',
    mDirect: 'Direct colour RGB565', mPal: '{n} colours', mKeep: 'Several palette sets (colour variants): uploads use the original colours.',
    mSheet: 'Sheet {w}×{h} px. Keep the size, otherwise the grid no longer fits.',
    mGroupRows: 'Rows: body (20), eyes (20), mouth (18), small body (24), small eyes (14), small mouth (14).',
    notRom: 'This is not a Paradise dump: {e}', loaded: '{ed} loaded: {s} sprites, {f} characters.',
    saved: 'Saved: {name}\n{n} sprites changed, {kb} KB still free.', nothing: 'Nothing changed yet.',
    tooBig: 'Too big for the free space. {e}',
    upOk: 'Applied: {n}', upSkip: 'Skipped: {list}', upNone: 'No matching files. Expected: figur_0237.png or sprite_0014.png.',
    wrongSize: '{f}: {e}', resetOk: 'Original restored.', dimOk: 'Card character {d} now sits in character {g}.', eggOk: 'Egg replaced.',
    numOk: 'Every character now shows its number. Save, flash, raise and note which number shows up at which stage.',
    notDim: 'Unknown card: {e}', leave: 'There are unsaved changes.',
    roleFallback: 'Fallback', roleBaby: 'Baby', roleAdult: 'first adult',
    eyeHint: 'Pink frame = eye. It moves into the eye layer so the character blinks and the cell has eyes. Wrong? Click on the eye.',
    eyeNone: 'Character has no visible eyes', dimEyesTooBig: 'The eyes were too detailed, the character keeps no eye layer.',
    roleFallbackText: 'appears when the game cannot load a character. Best left unchanged.',
    roleBabyText: 'hatches from the egg.', roleAdultText: 'from here on every character has its own eyes.',
    mEyes: 'Eyes keep their byte size, otherwise the fallback character hatches. Eyes that are too detailed are rejected.',
    mColorsFig: 'If all colours come from the original, the game keeps recolouring the character per tama. New colours become fixed (up to 8 colours plus outline).',
  },
};
let lang = 'de';
try { lang = localStorage.getItem('paramix-lang') || (navigator.language || 'de').slice(0, 2); } catch (e) {}
if (!T[lang]) lang = 'en';
function t(k, vars) { let s = (T[lang][k] ?? T.en[k] ?? k); if (vars) for (const [a, b] of Object.entries(vars)) s = s.split('{' + a + '}').join(b); return s; }
/* Messages of the modules (core, emu, doom, feld) are English; the German page
   translates them here. Placeholders ($1, $2) carry numbers and names. */
const MSG_DE = [
  [/^no ARC2 archive at (\S+)$/, 'kein ARC2-Archiv bei $1'],
  [/^unknown bpp (\S+)$/, 'unbekannte Farbtiefe $1'],
  [/^subimage count does not match$/, 'Anzahl der Teilbilder passt nicht'],
  [/^subimage size does not match$/, 'Grösse der Teilbilder passt nicht'],
  [/^eye image too detailed: (\d+) bytes, at most (\d+) allowed$/, 'Augenbild zu detailliert: $1 Byte, höchstens $2 erlaubt'],
  [/^not a Paradise flash dump \(SONIXDEV header missing\)$/, 'kein Paradise-Flash-Dump (SONIXDEV-Kopf fehlt)'],
  [/^unexpected asset archive \((\d+) files\)$/, 'unerwartetes Archiv ($1 Dateien)'],
  [/^too big: assets would end at (\S+), free space ends at (\S+)$/, 'zu gross: die Daten würden bei $1 enden, der freie Platz endet bei $2'],
  [/^checksum wrong at (\S+)$/, 'Prüfsumme falsch bei $1'],
  [/^sheet must be (\S+) px, got (\S+)$/, 'das Sheet muss $1 px gross sein, ist aber $2'],
  [/^character sheet must be (\S+) px, got (\S+)$/, 'das Figuren-Sheet muss $1 px gross sein, ist aber $2'],
  [/^no sprite package$/, 'kein Sprite-Paket'],
  [/^not a ZIP file$/, 'keine ZIP-Datei'],
  [/^unsupported ZIP compression in (.+)$/, 'nicht unterstützte ZIP-Kompression in $1'],
  [/^firmware not recognised \(expected a 4 MB or 8 MB image of a colour Tamagotchi\)$/, 'Firmware nicht erkannt (erwartet: 4-MB- oder 8-MB-Abbild eines Farb-Tamas)'],
  [/^scene table not found$/, 'Szenentabelle nicht gefunden'],
  [/^firmware too big: (\d+) blocks$/, 'Firmware zu gross: $1 Blöcke'],
  [/^launcher template does not fit$/, 'Starter-Vorlage passt nicht'],
  [/^the Paradise image must be 16 MB$/, 'das Paradise-Abbild muss 16 MB gross sein'],
  [/^home screen not found$/, 'Heimbildschirm nicht gefunden'],
  [/^area (\S+) is in use$/, 'Bereich $1 ist belegt'],
  [/^emulator program too big$/, 'Emulator-Programm zu gross'],
  [/^program too big$/, 'Programm zu gross'],
  [/^settings block missing in the program$/, 'Einstellungsblock im Programm fehlt'],
  [/^no emulator built in$/, 'kein Emulator eingebaut'],
  [/^no download content in the file$/, 'kein Download-Inhalt in der Datei'],
  [/^content checksum does not match$/, 'Prüfsumme des Inhalts stimmt nicht'],
  [/^this content fits no store of the 8 MB firmware$/, 'dieser Inhalt passt in kein Fach der 8-MB-Firmware'],
  [/^content too big for its store$/, 'Inhalt zu gross für sein Fach'],
  [/^only for the 8 MB firmware$/, 'nur für 8-MB-Firmware'],
  [/^no matching store$/, 'kein passendes Fach'],
  [/^already added$/, 'schon drin'],
  [/^(.+): all slots used, remove one first$/, (m, a) => storeDe(a) + ': alle Plätze belegt, zuerst einen entfernen'],
  [/^no such slot$/, 'kein solcher Platz'],
  [/^the menu is already built in$/, 'das Menü ist schon eingebaut'],
  [/^more fields need a Tropical, Glacier or Jade Forest ROM with the original firmware$/, 'mehr Felder gehen nur mit einem Tropical-, Glacier- oder Jade-Forest-ROM mit Original-Firmware'],
  [/^rain pictures too big$/, 'Regenbilder zu gross'], [/^area 0xF00000 is in use$/, 'Bereich 0xF00000 ist belegt'], [/^unexpected Jade Forest data$/, 'unerwartete Jade-Forest-Daten'], [/^helper page full$/, 'Hilfsseite voll'], [/^table (\S+) missing$/, 'Tabelle $1 fehlt'],
  [/^the second ROM must be a Land, Water or Sky ROM$/, 'das zweite ROM muss ein Land-, Water- oder Sky-ROM sein'],
  [/^six fields program missing$/, 'Programm für 6 Felder fehlt'],
  [/^unexpected map picture$/, 'unerwartetes Feldbild'],
  [/^unexpected Land picture (\d+)$/, 'unerwartetes Land-Bild $1'],
  [/^composite table full$/, 'Bildtabelle voll'],
  [/^menu program too old for six fields$/, 'Menüprogramm zu alt für 6 Felder'],
  [/^edition not recognised$/, 'Ausgabe nicht erkannt'],
  [/^nothing chosen$/, 'nichts gewählt'],
  [/^no room for the launcher$/, 'kein Platz für den Starter'],
  [/^no room for the menu next to DOOM$/, 'neben DOOM ist kein Platz für das Menü'],
  [/^no room for the saves next to DOOM$/, 'neben DOOM ist kein Platz für die Spielstände'],
  [/^menu program too big$/, 'Menü-Programm zu gross'],
  [/^the area for the menu is in use$/, 'der Bereich für das Menü ist belegt'],
  [/^no room for the saves$/, 'kein Platz für die Spielstände'],
  [/^the area for the saves is in use$/, 'der Bereich für die Spielstände ist belegt'],
  [/^settings block of the menu missing$/, 'Einstellungsblock des Menüs fehlt'],
  [/^the stored save of (.+) does not fit here$/, 'der gespeicherte Spielstand von $1 passt hier nicht hinein'],
  [/^not a WAD file$/, 'keine WAD-Datei'],
  [/^no DOOM maps in the WAD \(DOOM 2 is not supported\)$/, 'keine DOOM-Karten in der WAD (DOOM 2 wird nicht unterstützt)'],
  [/^no picture$/, 'kein Bild'],
  [/^broken$/, 'beschädigt'],
  [/^WAD conversion failed$/, 'WAD-Umwandlung fehlgeschlagen'],
  [/^the character data reaches (\S+), DOOM needs 0x830000 onwards$/, 'die Figuren-Daten reichen bis $1, DOOM braucht den Platz ab 0x830000'],
  [/^DOOM program too big$/, 'DOOM-Programm zu gross'],
  [/^WAD too big: (\d+) KB, room (\d+) KB \(try without music\)$/, 'WAD zu gross: $1 KB, Platz $2 KB (ohne Musik versuchen)'],
  [/^this DOOM has no key layout of its own yet$/, 'dieses DOOM kennt noch keine eigene Tastenbelegung'],
  [/^fire, forward and back need three different buttons$/, 'Schiessen, Vor und Zurück brauchen drei verschiedene Tasten'],
  [/^use needs its own button or the wheel$/, 'Benutzen braucht eine eigene Taste oder das Rad'],
];
function storeDe(label) { const k = ['Meals', 'Snacks', 'Clothing', 'Accessories', 'Items', 'Daily items', 'Wallpapers', 'Games', 'Destinations'].indexOf(label); return k < 0 ? label : t('dlcK' + k); }
function em(x) {
  const s = x && typeof x === 'object' ? (x.message || String(x)) : String(x || '');
  if (lang === 'en') return s;
  for (const [re, de] of MSG_DE) if (re.test(s)) return s.replace(re, de);
  return s;
}
function applyLang() {
  document.documentElement.lang = lang;
  document.querySelectorAll('[data-t]').forEach(el => { el.textContent = t(el.dataset.t); });
  document.querySelectorAll('[data-lang]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.lang === lang)));
  if (state.ctx) { renderInfo(); renderDim(); if (state.open) openDrawer(state.open.kind, state.open.idx); }
}
document.querySelectorAll('[data-lang]').forEach(b => b.onclick = () => { lang = b.dataset.lang; try { localStorage.setItem('paramix-lang', lang); } catch (e) {} applyLang(); });

// ------------------------------------------------------------------ state
const state = { ctx: null, name: '', changed: new Map(), dim: null, open: null, zoom: 2, decoded: new Map(), eyeOffsets: new Map(), eyeRects: new Map(), eyeLayer: false, fields: [], charNames: new Map() };
const entry = (i) => state.changed.get(i) || state.ctx.entries[i];
function decode(i) {
  const e = entry(i);
  let d = state.decoded.get(i);
  if (!d || d.src !== e) { d = { src: e, dec: C.spriteDecode(e) }; state.decoded.set(i, d); }
  return d.dec;
}

// ------------------------------------------------------------ helpers
function toast(msg, err) { const el = $('toast'); el.textContent = msg; el.className = 'toast on' + (err ? ' err' : ''); clearTimeout(toast.t); toast.t = setTimeout(() => el.className = 'toast', err ? 9000 : 5000); }
async function busy(fn) { $('busy').classList.add('on'); await new Promise(r => setTimeout(r, 30)); try { return await fn(); } finally { $('busy').classList.remove('on'); } }
function imgToCanvas(img, scale = 1, cv) {
  cv = cv || document.createElement('canvas'); cv.width = img.w * scale; cv.height = img.h * scale;
  const tmp = document.createElement('canvas'); tmp.width = img.w; tmp.height = img.h;
  tmp.getContext('2d').putImageData(new ImageData(img.data, img.w, img.h), 0, 0);
  const g = cv.getContext('2d'); g.imageSmoothingEnabled = false; g.clearRect(0, 0, cv.width, cv.height); g.drawImage(tmp, 0, 0, cv.width, cv.height);
  return cv;
}
function pngBlob(img) { return new Promise(r => imgToCanvas(img).toBlob(r, 'image/png')); }
async function pngBytes(img) { return new Uint8Array(await (await pngBlob(img)).arrayBuffer()); }
async function decodePng(blob) {
  const bmp = await createImageBitmap(blob, { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
  const cv = document.createElement('canvas'); cv.width = bmp.width; cv.height = bmp.height;
  const g = cv.getContext('2d', { willReadFrequently: true }); g.drawImage(bmp, 0, 0);
  const d = g.getImageData(0, 0, bmp.width, bmp.height);
  return { w: d.width, h: d.height, data: d.data };
}
function download(data, name, type) {
  const url = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], { type: type || 'application/octet-stream' }));
  const a = document.createElement('a'); a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}
async function inflateRaw(bytes) {
  const ds = new DecompressionStream('deflate-raw');
  const out = await new Response(new Blob([bytes]).stream().pipeThrough(ds)).arrayBuffer();
  return new Uint8Array(out);
}
const pad4 = (n) => String(n).padStart(4, '0');
const baseName = () => state.name.replace(/\.[^.]+$/, '').replace(/(_paramix|_emu|_doom_E\d|_felder|_fields|_items)+$/, '');

// ------------------------------------------------------------- loading
async function loadRom(file) {
  const buf = new Uint8Array(await file.arrayBuffer());
  let ctx;
  try { ctx = C.openRom(buf); } catch (e) { toast(t('notRom', { e: em(e) }), true); return; }
  Object.assign(state, { ctx, name: file.name, changed: new Map(), decoded: new Map(), open: null, eyeOffsets: new Map() });
  $('start').hidden = true; $('work').classList.add('on'); $('btnSave').disabled = false;
  $('romname').textContent = file.name;
  state.emu = null; state.emuRemove = false; state.doom = null; state.doomKeys = null; state.six = null; state.sixName = ''; state.sixWant = false;
  { const si = S6.sixInfo(buf); state.sixPlanet = si && si.version >= 5 ? si.planet : true; }
  { const fe = F.embedded(), f = fe && F.feldFind(buf, fe.stub);
    state.feld = !!(f && (f.flags & F.F_FIELDS)); state.bag = !!(f && (f.flags & F.F_BAG));
    state.feldInit = state.feld; state.bagInit = state.bag;
    // A menu from an older Studio version is rebuilt on save (e.g. 0.9.5 had no item pictures)
    state.feldStale = false;
    if (f) {
      try {
        const embOld = { stub: fe.stub, oldStub: E.embedded() && E.embedded().stub };
        const re = F.feldApply(F.feldStrip(buf, embOld, E), { fields: state.feld, bag: state.bag }, F.feldSlots(buf, fe.stub), { feld: fe.feld, stub: fe.stub }, E);
        state.feldStale = re.length !== buf.length || re.some((v, i) => v !== buf[i]);
      } catch (e) { state.feldStale = false; }
    } }
  // fields and character names (from the ROM's character table and text table)
  state.fields = []; state.charNames = new Map();
  try {
    state.fields = C.fieldSets(ctx);
    const tt = F.textTabelle(buf), chars = C.charTable(ctx.files[0]).filter((c) => c.id > 1000 && c.id < 9000);
    if (tt >= 0 && chars.length + 1 === ctx.groups.length) chars.forEach((c, i) => { const n = F.zeichenkette(buf, tt, c.name); if (n) state.charNames.set(ctx.groups[i + 1], n); });
  } catch (e) { state.fields = []; }
  renderInfo(); renderGrids(); renderDim(); renderEmu(); renderDoom(); renderFeld(); if (!dv.run) devRender();
  toast(t('loaded', { ed: ctx.edition, s: ctx.entries.length, f: ctx.groups.length }));
}
function estimateEnd() {
  const ctx = state.ctx; let pack = 4 * ctx.entries.length;
  for (let i = 0; i < ctx.entries.length; i++) { pack += (4 - pack % 4) % 4; pack += entry(i).length; }
  const al = (n) => n + ((4 - n % 4) % 4);
  return C.ASSET_BASE + 16 + 48 + al(ctx.files[0].length) + al(pack) + al(ctx.files[2].length);
}
function renderInfo() {
  const ctx = state.ctx; if (!ctx) return;
  const end = estimateEnd(), total = ctx.limit - C.ASSET_BASE, used = end - C.ASSET_BASE, free = ctx.limit - end;
  const pct = Math.min(100, used / total * 100);
  $('info').innerHTML = '';
  const add = (label, html) => { const s = document.createElement('span'); s.innerHTML = label + ': <b>' + html + '</b>'; $('info').appendChild(s); };
  add(t('edition'), ctx.edition); add(t('sprites'), ctx.entries.length); add(t('figures'), ctx.groups.length); add(t('changed'), state.changed.size);
  const s = document.createElement('span');
  s.innerHTML = t('space') + ': <span class="meter' + (free < 0 ? ' full' : '') + '"><i style="width:' + pct.toFixed(1) + '%"></i></span> <b>' + (free / 1024 | 0) + ' KB</b>';
  $('info').appendChild(s);
  renderPlan();
}

// --------------------------------------------------------------- grids
let observer;
function tile(kind, idx, label) {
  const b = document.createElement('button'); b.className = 'tile'; b.dataset.kind = kind; b.dataset.idx = idx;
  b.innerHTML = '<div class="stage"></div><div class="cap"><b>#' + idx + '</b> <span></span></div>';
  b.querySelector('.cap span').textContent = label || '';
  b.onclick = () => openDrawer(kind, idx);
  observer.observe(b);
  return b;
}
function paintTile(b) {
  const i = +b.dataset.idx, kind = b.dataset.kind;
  let img;
  try { const d = decode(i); img = d.subs[0]; } catch (e) { return; }
  const box = 96, sc = Math.max(1, Math.min(2, Math.floor(box / Math.max(img.w, img.h)) || 1));
  const cv = imgToCanvas(img, sc);
  if (img.w * sc > box || img.h * sc > box) { cv.style.width = '100%'; cv.style.height = 'auto'; }
  const st = b.querySelector('.stage'); st.innerHTML = ''; st.appendChild(cv);
  const ch = kind === 'fig' ? [0, 1, 2, 3, 4, 5].some(k => state.changed.has(i + k)) : state.changed.has(i);
  b.classList.toggle('changed', ch);
}
function renderGrids() {
  if (observer) observer.disconnect();
  observer = new IntersectionObserver((es) => es.forEach(e => { if (e.isIntersecting) { paintTile(e.target); observer.unobserve(e.target); } }), { rootMargin: '300px' });
  const ctx = state.ctx, gf = $('gridFigs'), ga = $('gridAll'); gf.innerHTML = ''; ga.innerHTML = '';
  for (const g of ctx.groups) gf.appendChild(tile('fig', g, [state.charNames.get(g), groupRole(g)].filter(Boolean).join(' ')));
  renderFieldPick();
  ctx.entries.forEach((e, i) => { const h = C.spriteHeader(e); ga.appendChild(tile('spr', i, h.sw * Math.max(1, h.iw) + '×' + h.sh * Math.max(1, h.ih))); });
}
function refreshTile(kind, idx) {
  document.querySelectorAll('.tile[data-idx="' + idx + '"][data-kind="' + kind + '"]').forEach(paintTile);
  if (kind === 'spr') { const g = state.ctx.groups.find(g => idx >= g && idx < g + 6); if (g !== undefined) refreshTile('fig', g); }
  else for (let k = 0; k < 6; k++) document.querySelectorAll('.tile[data-idx="' + (idx + k) + '"][data-kind="spr"]').forEach(paintTile);
  renderInfo();
}
document.querySelectorAll('.tab').forEach(b => b.onclick = () => {
  document.querySelectorAll('.tab').forEach(x => x.setAttribute('aria-selected', String(x === b)));
  ['figs', 'all', 'dim', 'emu', 'doom', 'feld'].forEach(k => $('pane-' + k).hidden = k !== b.dataset.tab);
});

// Options for a sprite that belongs to a character group (eyes keep their length,
// colours avoid the slots the game recolours). Empty for other sprites.
function partOpts(i) {
  const g = state.ctx.groups.find(g => i >= g && i < g + 6);
  return g === undefined ? {} : C.partOptions(i - g);
}
function groupRole(g) {
  const ctx = state.ctx;
  if (g === ctx.groups[0]) return t('roleFallback');
  if (ctx.egg >= 0 && g === ctx.egg + 6) return t('roleBaby');
  if (g === ctx.adult) return t('roleAdult');
  return '';
}

// --------------------------------------------------------------- sheets
function sheetOf(kind, idx) {
  if (kind === 'fig') return C.groupSheet([0, 1, 2, 3, 4, 5].map(k => decode(idx + k).subs));
  return C.sheetFromSubs(decode(idx).subs);
}
const sheetName = (kind, idx) => (kind === 'fig' ? 'figur_' : 'sprite_') + pad4(idx) + '.png';
/** Apply an edited sheet. Unchanged parts keep their original bytes. */
function applySheet(kind, idx, img) {
  const opts = { allowMoreColors: $('optColors').checked };
  // encode first, change nothing until every part is accepted (a character is never half replaced)
  const plan = [];
  const setEntry = (i, subs) => {
    const cur = decode(i).subs;
    if (subs.length === cur.length && subs.every((s, k) => C.sameImage(s, cur[k]))) return;
    const orig = state.ctx.entries[i];
    const od = C.spriteDecode(orig).subs;
    if (subs.every((s, k) => C.sameImage(s, od[k]))) { plan.push([i, null]); return; }
    plan.push([i, C.spriteEncode(orig, subs, Object.assign({}, opts, partOpts(i)))]);
  };
  const commit = () => plan.forEach(([i, e]) => { if (e) state.changed.set(i, e); else state.changed.delete(i); });
  if (kind === 'fig') { C.groupFromSheet(img).forEach((subs, k) => setEntry(idx + k, subs)); commit(); return; }
  const d = decode(idx); setEntry(idx, C.subsFromSheet(img, d.subs[0].w, d.subs[0].h, d.subs.length)); commit();
}
function parseName(name) {
  const m = /(figur|figure|sprite)_(\d+)\.png$/i.exec(name.split('/').pop());
  if (!m) return null;
  return { kind: /sprite/i.test(m[1]) ? 'spr' : 'fig', idx: +m[2] };
}
async function uploadFiles(files) {
  const items = [];
  for (const f of files) {
    if (/\.zip$/i.test(f.name)) {
      const z = await C.zipRead(new Uint8Array(await f.arrayBuffer()), inflateRaw);
      for (const e of z) items.push({ name: e.name, blob: new Blob([e.data], { type: 'image/png' }) });
    } else items.push({ name: f.name, blob: f });
  }
  let ok = 0; const skip = [];
  for (const it of items) {
    const p = parseName(it.name);
    if (!p) { if (/\.png$/i.test(it.name)) skip.push(it.name.split('/').pop()); continue; }
    if (p.kind === 'fig' && !state.ctx.groups.includes(p.idx)) { skip.push(it.name.split('/').pop()); continue; }
    if (p.kind === 'spr' && !(p.idx >= 0 && p.idx < state.ctx.entries.length)) { skip.push(it.name.split('/').pop()); continue; }
    try { applySheet(p.kind, p.idx, await decodePng(it.blob)); ok++; refreshTile(p.kind, p.idx); }
    catch (e) { skip.push(t('wrongSize', { f: it.name.split('/').pop(), e: em(e) })); }
  }
  if (!ok && !skip.length) toast(t('upNone'), true);
  else toast(t('upOk', { n: ok }) + (skip.length ? '\n' + t('upSkip', { list: skip.slice(0, 6).join(', ') + (skip.length > 6 ? ' …' : '') }) : ''), skip.length > 0 && !ok);
  if (state.open) openDrawer(state.open.kind, state.open.idx);
}
/* Fields (worlds): filter the character grid and export everything of one field */
function renderFieldPick() {
  const sel = $('figField'), cur = sel.value; sel.innerHTML = '';
  const o = document.createElement('option'); o.value = ''; o.textContent = t('figAll'); sel.appendChild(o);
  state.fields.forEach((f) => { const x = document.createElement('option'); x.value = f.type; x.textContent = f.name; sel.appendChild(x); });
  sel.value = state.fields.some((f) => String(f.type) === cur) ? cur : '';
  applyFieldPick();
}
function applyFieldPick() {
  const f = state.fields.find((x) => String(x.type) === $('figField').value);
  const set = f ? new Set(f.chars.map((c) => c.group)) : null;
  document.querySelectorAll('#gridFigs .tile').forEach((b) => { b.hidden = !!set && !set.has(+b.dataset.idx); });
  $('btnZipField').hidden = !f; $('btnFieldIn').hidden = !f;
}
$('figField').onchange = applyFieldPick;
$('btnZipField').onclick = () => busy(async () => {
  const f = state.fields.find((x) => String(x.type) === $('figField').value); if (!f) return;
  const files = [], lines = ['Paramix Studio: ' + f.name + ' (' + state.ctx.edition + ')', ''];
  for (const c of f.chars) { files.push({ name: sheetName('fig', c.group), data: await pngBytes(sheetOf('fig', c.group)) }); lines.push(sheetName('fig', c.group) + '  stage ' + c.stage + '  ' + (state.charNames.get(c.group) || '')); }
  const pics = [...f.maps, ...f.overlays, ...f.closeups];
  for (const i of pics) files.push({ name: sheetName('spr', i), data: await pngBytes(sheetOf('spr', i)) });
  if (f.maps.length) lines.push('', 'field map: ' + f.maps.map((i) => sheetName('spr', i)).join(' '));
  if (f.overlays.length) lines.push('map overlays: ' + f.overlays.map((i) => sheetName('spr', i)).join(' '));
  if (f.closeups.length) lines.push('close-up backgrounds: ' + f.closeups.map((i) => sheetName('spr', i)).join(' '));
  files.push({ name: 'contents.txt', data: new TextEncoder().encode(lines.join('\n') + '\n') });
  const meta = { paramix: 'field', edition: state.ctx.edition, type: f.type, name: f.name, chars: f.chars.map((c) => ({ id: c.id, stage: c.stage, group: c.group })), maps: f.maps, overlays: f.overlays, closeups: f.closeups };
  files.push({ name: 'field.json', data: new TextEncoder().encode(JSON.stringify(meta, null, 1)) });
  download(C.zipBuild(files), baseName() + '_' + f.name.toLowerCase() + '.zip', 'application/zip');
  toast(t('fieldZipOk', { f: f.name, n: f.chars.length, s: pics.length }));
});
/* Load a field ZIP (from this or another ROM) into the selected field: characters in
   the same order (child, teens, adults), field map, overlays and close-ups one to one. */
$('btnFieldIn').onclick = () => $('fileField').click();
$('fileField').onchange = (e) => {
  const file = e.target.files[0]; e.target.value = ''; if (!file) return;
  busy(async () => {
    const dst = state.fields.find((x) => String(x.type) === $('figField').value); if (!dst) return;
    const z = await C.zipRead(new Uint8Array(await file.arrayBuffer()), inflateRaw);
    const by = new Map(z.map((x) => [x.name.split('/').pop(), x.data]));
    let meta = null; try { meta = JSON.parse(new TextDecoder().decode(by.get('field.json'))); } catch (err) { meta = null; }
    if (!meta || meta.paramix !== 'field') { toast(t('fieldInBad'), true); return; }
    let n = 0, eyes = 0; const skip = [];
    const put = async (kind, srcIdx, dstIdx) => {
      const nm = sheetName(kind, srcIdx), data = by.get(nm); if (!data || dstIdx === undefined) return;
      try { applySheet(kind, dstIdx, await decodePng(new Blob([data], { type: 'image/png' }))); n++; refreshTile(kind, dstIdx); }
      catch (err) { if (/^eye image too detailed/.test(err.message)) eyes++; else skip.push(t('wrongSize', { f: nm, e: em(err) })); }
    };
    for (let i = 0; i < meta.chars.length && i < dst.chars.length; i++) await put('fig', meta.chars[i].group, dst.chars[i].group);
    for (let i = 0; i < meta.maps.length && i < dst.maps.length; i++) await put('spr', meta.maps[i], dst.maps[i]);
    for (let i = 0; i < meta.overlays.length && i < dst.overlays.length; i++) await put('spr', meta.overlays[i], dst.overlays[i]);
    for (let i = 0; i < meta.closeups.length && i < dst.closeups.length; i++) await put('spr', meta.closeups[i], dst.closeups[i]);
    toast(t('fieldInOk', { src: meta.name + ' (' + meta.edition + ')', dst: dst.name, n }) + (eyes ? '\n' + t('fieldInEyes', { k: eyes }) : '') + (skip.length ? '\n' + t('upSkip', { list: skip.slice(0, 4).join(', ') + (skip.length > 4 ? ' …' : '') }) : ''), skip.length > 0 && !n);
  });
};
async function zipSheets(kind) {
  const list = kind === 'fig' ? state.ctx.groups : state.ctx.entries.map((e, i) => i);
  const files = [];
  for (const i of list) files.push({ name: sheetName(kind, i), data: await pngBytes(sheetOf(kind, i)) });
  download(C.zipBuild(files), baseName() + (kind === 'fig' ? '_figuren.zip' : '_sprites.zip'), 'application/zip');
}

// --------------------------------------------------------------- drawer
function openDrawer(kind, idx) {
  state.open = { kind, idx };
  const img = sheetOf(kind, idx);
  $('dTitle').textContent = (kind === 'fig' ? t('figure') : t('sprite')) + ' #' + idx;
  imgToCanvas(img, state.zoom, $('dCanvas'));
  const meta = $('dMeta'); meta.innerHTML = '';
  const li = (s) => { const x = document.createElement('li'); x.textContent = s; meta.appendChild(x); };
  li(t('mSheet', { w: img.w, h: img.h }));
  if (kind === 'fig') { li(t('mGroupRows')); li(t('mEyes')); li(t('mColorsFig')); if (groupRole(idx)) li(groupRole(idx) + ': ' + t(idx === state.ctx.groups[0] ? 'roleFallbackText' : idx === state.ctx.adult ? 'roleAdultText' : 'roleBabyText')); }
  else {
    const h = C.spriteHeader(state.ctx.entries[idx]);
    li(t('mSize', { w: h.sw, h: h.sh, n: h.numSprites / (Math.max(1, h.iw) * Math.max(1, h.ih)), iw: Math.max(1, h.iw), ih: Math.max(1, h.ih) }));
    li(t('mColors', { c: h.bpp >= 0x10 ? t('mDirect') : t('mPal', { n: 1 << [1, 2, 4, 8][h.bpp] }), p: h.bpp >= 0x10 ? 0 : Math.max(1, h.numPal) }));
    if (h.numPal > 1) li(t('mKeep'));
  }
  const ch = kind === 'fig' ? [0, 1, 2, 3, 4, 5].some(k => state.changed.has(idx + k)) : state.changed.has(idx);
  $('dReset').disabled = !ch;
  $('drawer').classList.add('open'); $('drawer').setAttribute('aria-hidden', 'false'); $('scrim').classList.add('on');
}
function closeDrawer() { state.open = null; $('drawer').classList.remove('open'); $('drawer').setAttribute('aria-hidden', 'true'); $('scrim').classList.remove('on'); }
$('dClose').onclick = closeDrawer; $('scrim').onclick = closeDrawer;
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeDrawer(); });
document.querySelectorAll('[data-z]').forEach(b => b.onclick = () => { state.zoom = +b.dataset.z; if (state.open) openDrawer(state.open.kind, state.open.idx); });
$('dDown').onclick = async () => { const { kind, idx } = state.open; download(await pngBlob(sheetOf(kind, idx)), sheetName(kind, idx)); };
$('dUp').onclick = () => $('fileOne').click();
$('fileOne').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f || !state.open) return;
  const { kind, idx } = state.open;
  try { await busy(async () => applySheet(kind, idx, await decodePng(f))); refreshTile(kind, idx); openDrawer(kind, idx); toast(t('upOk', { n: 1 })); }
  catch (err) { toast(t('wrongSize', { f: f.name, e: em(err) }), true); }
};
$('dReset').onclick = () => {
  const { kind, idx } = state.open;
  if (kind === 'fig') for (let k = 0; k < 6; k++) state.changed.delete(idx + k); else state.changed.delete(idx);
  refreshTile(kind, idx); openDrawer(kind, idx); toast(t('resetOk'));
};

// ------------------------------------------------------------------ card
function eyeRectFor(ch) {
  if (!state.eyeRects) state.eyeRects = new Map();
  if (state.eyeRects.has(ch)) return state.eyeRects.get(ch);
  const pre = C.dimEyePreset(ch);
  const r = pre === undefined ? C.dimGuessEye(ch) : pre;
  state.eyeRects.set(ch, r); return r;
}
/** Insert a card character into group g (body, eye layer, blank mouth). */
function insertDim(ch, g) {
  const ctx = state.ctx;
  if (!state.eyeLayer) {
    // Standard: the card character keeps its own eyes in the body. The game draws the eye
    // image of the current character on top (field and cell), so it stays empty.
    const parts = C.dimCharParts(ch);
    [0, 1, 2, 3, 4, 5].forEach(k => {
      const orig = ctx.entries[g + k];
      state.changed.set(g + k, k === 0 || k === 3 ? C.spriteEncode(orig, parts[k], C.partOptions(k)) : C.spriteBlank(orig, C.partOptions(k)));
    });
    state.eyeOffsets.delete(g);
    return true;
  }
  const rect = eyeRectFor(ch);
  let res = rect ? C.dimCharPartsWithEyes(ch, rect) : null;
  const parts = res ? res.parts : C.dimCharParts(ch);
  [0, 2, 3, 5].forEach(k => {
    const orig = ctx.entries[g + k];
    state.changed.set(g + k, k === 0 || k === 3 ? C.spriteEncode(orig, parts[k], C.partOptions(k)) : C.spriteBlank(orig, C.partOptions(k)));
  });
  let eyesOk = !!res;
  for (const [k, layer] of [[1, res && res.big], [4, res && res.small]]) {
    const orig = ctx.entries[g + k]; let enc = null;
    if (layer) for (const v of C.eyeVariants(layer, k === 1)) { try { enc = C.spriteEncode(orig, v, C.partOptions(k)); break; } catch (e) { /* too big, fewer open eyes */ } }
    if (!enc) eyesOk = false;
    state.changed.set(g + k, enc || C.spriteBlank(orig, C.partOptions(k)));
  }
  if (!state.eyeOffsets) state.eyeOffsets = new Map();
  if (eyesOk) state.eyeOffsets.set(g, [res.eyeBig, res.eyeSmall]); else state.eyeOffsets.delete(g);
  return eyesOk;
}
/** Data pack with the eye layer placed for every inserted character still changed. */
function currentDataPack() {
  let dp = state.ctx.files[0];
  if (!state.eyeOffsets) return null;
  let any = false;
  for (const [g, v] of state.eyeOffsets) {
    if (v === 'hide') {
      if (!state.changed.has(g)) continue;
      dp = C.hideEyes(dp, g, g + 1).dataPack; dp = C.hideEyes(dp, g + 3, g + 4).dataPack;
      any = true; continue;
    }
    const [b, sm] = v;
    if (!state.changed.has(g + 1)) continue;
    dp = C.setEyeOffset(dp, g, g + 1, 64, 32, b[0], b[1]).dataPack;
    dp = C.setEyeOffset(dp, g + 3, g + 4, 32, 16, sm[0], sm[1]).dataPack;
    any = true;
  }
  return any ? dp : null;
}
function renderDim() {
  const area = $('dimArea'); area.innerHTML = '';
  if (!state.dim) { const p = document.createElement('p'); p.className = 'hint'; p.textContent = t('dimNone'); area.appendChild(p); return; }
  const dim = state.dim, ctx = state.ctx;
  const bar = document.createElement('div'); bar.className = 'assign';
  const selD = document.createElement('select'); dim.chars.forEach(c => { const o = document.createElement('option'); o.value = c.index; o.textContent = t('dimNr') + ' ' + c.index + (c.baby ? ' (' + t('dimBaby') + ')' : ''); selD.appendChild(o); });
  const selG = document.createElement('select'); ctx.groups.forEach(g => { const o = document.createElement('option'); o.value = g; o.textContent = t('figure') + ' #' + g + (groupRole(g) ? ' (' + groupRole(g) + ')' : ''); selG.appendChild(o); }); if (ctx.groups.includes(ctx.adult)) selG.value = ctx.adult;
  const go = document.createElement('button'); go.className = 'btn primary small'; go.textContent = t('dimApply');
  go.onclick = () => busy(async () => {
    const ch = dim.chars[+selD.value], g = +selG.value;
    const ok = insertDim(ch, g);
    refreshTile('fig', g); toast(t('dimOk', { d: ch.index, g }) + (state.eyeLayer && eyeRectFor(ch) && !ok ? '\n' + t('dimEyesTooBig') : ''));
  });
  const l1 = document.createElement('span'); l1.textContent = t('dimPick');
  const l2 = document.createElement('span'); l2.textContent = t('dimTarget');
  bar.append(l1, selD, l2, selG, go);
  if (dim.eggs.length && ctx.egg >= 0) {
    const eg = document.createElement('button'); eg.className = 'btn small'; eg.textContent = t('dimEgg');
    eg.onclick = () => {
      const e = C.dimEggSubs(dim.eggs);
      state.changed.set(ctx.egg, C.spriteEncode(ctx.entries[ctx.egg], e.small, {}));
      state.changed.set(ctx.egg + 3, C.spriteEncode(ctx.entries[ctx.egg + 3], e.big, {}));
      refreshTile('spr', ctx.egg); refreshTile('spr', ctx.egg + 3); toast(t('eggOk'));
    };
    bar.appendChild(eg);
  }
  area.appendChild(bar);
  // eye editor: the chosen card character large, the eye frame drawn, click to move it
  const ed = document.createElement('div'); ed.className = 'eyeed';
  const cv = document.createElement('canvas'); cv.className = 'eyecv';
  const side = document.createElement('div');
  const hint = document.createElement('p'); hint.className = 'hint'; hint.textContent = t('eyeHint');
  const none = document.createElement('label'); none.className = 'check';
  const cb = document.createElement('input'); cb.type = 'checkbox'; none.append(cb, document.createTextNode(' ' + t('eyeNone')));
  side.append(hint, none); ed.append(cv, side); if (state.eyeLayer) area.appendChild(ed);
  const S = 5;
  const draw = () => {
    const ch = dim.chars[+selD.value], im = ch.frames.idle1, r = eyeRectFor(ch);
    imgToCanvas(im, S, cv);
    const g2 = cv.getContext('2d');
    if (r) { g2.strokeStyle = '#ff2fa0'; g2.lineWidth = 2; g2.strokeRect(r[0] * S - 1, r[1] * S - 1, (r[2] - r[0] + 1) * S + 2, (r[3] - r[1] + 1) * S + 2); }
    cb.checked = !r;
  };
  cv.onclick = (e) => {
    const ch = dim.chars[+selD.value], b = cv.getBoundingClientRect();
    const x = Math.floor((e.clientX - b.left) / b.width * ch.frames.idle1.w), y = Math.floor((e.clientY - b.top) / b.height * ch.frames.idle1.h);
    state.eyeRects.set(ch, C.eyeRectAt(ch, x, y)); draw();
  };
  cb.onchange = () => { const ch = dim.chars[+selD.value]; state.eyeRects.set(ch, cb.checked ? null : (C.dimEyePreset(ch) || C.dimGuessEye(ch))); draw(); };
  selD.onchange = () => { state.dimSel = selD.value; draw(); };
  if (state.dimSel === undefined) { const c = dim.chars.find(x => !x.baby); if (c) state.dimSel = String(c.index); }
  if (state.dimSel !== undefined) selD.value = state.dimSel;
  const grid = document.createElement('div'); grid.className = 'dimgrid';
  dim.chars.forEach(c => {
    const b = document.createElement('button'); b.className = 'tile';
    b.innerHTML = '<div class="stage"></div><div class="cap"><b>' + t('dimNr') + ' ' + c.index + '</b>' + (c.baby ? ' ' + t('dimBaby') : '') + '</div>';
    b.querySelector('.stage').appendChild(imgToCanvas(c.frames.idle1, c.baby ? 3 : 1));
    b.onclick = () => { selD.value = c.index; state.dimSel = selD.value; grid.querySelectorAll('.tile').forEach(x => x.classList.toggle('sel', x === b)); draw(); };
    grid.appendChild(b);
  });
  area.appendChild(grid);
  draw();
}
$('btnDim').onclick = () => $('fileDim').click();
$('fileDim').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try { state.dim = C.dimLoad(new Uint8Array(await f.arrayBuffer())); state.dimSel = undefined; renderDim(); }
  catch (err) { toast(t('notDim', { e: em(err) }), true); }
};

// ------------------------------------------------------------ overview
const MB = (n) => (n / 1048576).toFixed(n < 1048576 ? 2 : 1).replace(/\.?0+$/, '') + ' MB';
function renderPlan() {
  const el = $('plan'); if (!el || !state.ctx) return;
  const rom = state.ctx.rom, kind = appInstalled();
  const emuOn = !!state.emu || (kind === 2 && !state.emuRemove && !state.doom), doomOn = !!state.doom || (kind === 1 && !state.emuRemove && !state.emu);
  const fe = F.embedded(), menuOld = fe && F.feldFind(rom, fe.stub);
  const st = (on, isNew, old, extra) => on ? (isNew ? t('stNew') : t('stKeep')) + (extra ? ', ' + extra : '') : (old ? t('stDel') : t('stNone'));
  const chips = [
    [t('planFig'), state.changed.size ? t('stChanged', { n: state.changed.size }) : t('stSame'), state.changed.size ? 'new' : 'keep'],
    [t('planEmu'), emuUpPending() ? t('stUpd') : st(emuOn, !!state.emu, kind === 2, state.emu ? E.DEVICES[state.emu.dev].label : ''), state.emu || emuUpPending() ? 'new' : emuOn ? 'keep' : kind === 2 ? 'del' : 'off'],
    [t('planDoom'), st(doomOn, !!state.doom, kind === 1, state.doom ? 'E' + $('doomEp').value + ($('doomMusic').checked ? ' + ' + t('doomMusic') : '') : ''), state.doom ? 'new' : doomOn ? 'keep' : kind === 1 ? 'del' : 'off'],
  ];
  const menuOn = state.feld || state.bag, parts = [state.feld ? t('feldOn') : '', state.bag ? t('bagOn') : ''].filter(Boolean).join(', ');
  chips.push([t('planMenu'), menuOn ? parts + (state.feldStale && state.feld === state.feldInit && state.bag === state.bagInit ? ' (' + t('stUpd') + ')' : '') : (menuOld ? t('stDel') : t('stNone')), menuOn ? ((state.feld === state.feldInit && state.bag === state.bagInit && !state.feldStale) ? 'keep' : 'new') : menuOld ? 'del' : 'off']);
  const sixIn = !!S6.sixInfo(rom);
  if (sixIn || state.six || state.sixWant) chips.push([t('planSix'), sixIn ? t('stKeep') : t('stNew'), sixIn ? 'keep' : 'new']);
  // Flash: from 0x111000 to the end
  const base = 0x111000, end = 0x1000000, total = end - base, segs = [];
  const assets = estimateEnd() - base + (state.six && !sixIn ? 0xB9000 : 0); segs.push(['fw', t('segFw'), assets]);
  let doomSize = 0, emuSize = 0;
  if (state.doom) { const f = state.doom.fit[$('doomEp').value]; doomSize = (f ? ($('doomMusic').checked && f.music ? f.sizeM : f.sizeP) : 0) + (0xD48000 - 0xCA0000); }
  else if (doomOn) doomSize = (rom[0x830000] | rom[0x830001] << 8 | rom[0x830002] << 16 | rom[0x830003] << 24) + (0xD48000 - 0xCA0000);
  if (state.emu) emuSize = state.emu.size || 0;
  else if (emuOn) { const o = 0xA0000 + 0x140 + 12; emuSize = rom[o] | rom[o + 1] << 8 | rom[o + 2] << 16 | rom[o + 3] << 24; }
  if (doomOn) segs.push(['doom', t('segDoom'), doomSize]);
  if (emuOn) segs.push(['emu', t('segEmu'), emuSize]);
  if (state.feld && !doomOn) { const ed = F.edition(rom); segs.push(['slots', t('segSlots'), ed ? ed.profile.names.length * 0x58000 : 0]); }
  const save = end - 0xD29000; segs.push(['save', t('segSave'), save]);
  const used = segs.reduce((a, x) => a + x[2], 0); segs.push(['free', t('segFree'), Math.max(0, total - used)]);
  el.innerHTML = '';
  const h = document.createElement('div'); h.className = 'plan-h'; h.textContent = t('planTitle'); el.appendChild(h);
  const row = document.createElement('div'); row.className = 'chips';
  chips.forEach(([l, v, c]) => { const d = document.createElement('span'); d.className = 'chip ' + c; d.innerHTML = '<b></b> <span></span>'; d.firstChild.textContent = l; d.lastChild.textContent = v; row.appendChild(d); });
  el.appendChild(row);
  const bar = document.createElement('div'); bar.className = 'flash';
  const leg = document.createElement('div'); leg.className = 'legend';
  segs.forEach(([k, l, n]) => {
    if (n <= 0) return;
    const i = document.createElement('i'); i.className = 'seg ' + k; i.style.width = (n / total * 100).toFixed(2) + '%'; i.title = l + ': ' + MB(n); bar.appendChild(i);
    const g = document.createElement('span'); g.innerHTML = '<i class="dot ' + k + '"></i>'; g.appendChild(document.createTextNode(l + ' ' + MB(n))); leg.appendChild(g);
  });
  el.appendChild(bar); el.appendChild(leg);
  const r = document.createElement('div'); r.className = 'rule'; r.textContent = appRoom() ? t('ruleBoth') : t('ruleNoRoom'); el.appendChild(r);
}

// ------------------------------------------------------------ emulator
function appInstalled() {
  try { return E.installedOrigs(state.ctx.rom, E.sceneTable(state.ctx.rom)) ? F.appKind(state.ctx.rom) : 0; } catch (e) { return 0; }
}
function emuInstalled() { return appInstalled() === 2; }
function doomInstalled() { return appInstalled() === 1; }
function emuUpPending() {
  const emb = E.embedded();
  if (state.emu || state.emuRemove || state.doom || !emb || !emuInstalled()) return false;
  try { return !!E.emuUpgrade(state.ctx.rom, emb.app); } catch (e) { return false; }
}
/* Room for emulator and DOOM: both need the area after the characters from 0x830000 and the program space at 0x0A0000 */
function appRoom() { const r = state.ctx.rom; return estimateEnd() <= 0x830000 && (F.appKind(r) || r.subarray(0xA0000, 0xA1000).every(b => b === 0xFF)); }
function renderEmu() {
  if (!state.ctx) return;
  const has = emuInstalled();
  $('btnEmuOff').hidden = !has || state.emuRemove;
  let s;
  if (!E.embedded()) s = t('emuMissing');
  else if (state.emu) s = t('emuOk', { size: E.DEVICES[state.emu.dev].label });
  else if (state.emuRemove) s = t('emuGone');
  else s = has ? t('emuHas') + (emuUpPending() ? ' ' + t('emuUp') : '') : t('emuNone');
  s += (state.emu || (has && !state.emuRemove) ? '\n' + t('emuUse') : '');
  const room = appRoom();
  $('btnEmu').disabled = !room;
  if (!room) s = t('noRoomApp');
  else if (!state.emu && (state.doom || (doomInstalled() && !state.emuRemove))) s += '\n' + t('emuVsDoom');
  $('emuStat').textContent = s;
  renderPlan();
  const big = state.emu && state.emu.dev === 'idl';
  $('emuDlc').hidden = !big;
  if (big) renderDlc();
}
/* Download content of the 8 MB guest firmware: one box per store, every slot with its name */
function renderDlc() {
  const box = $('dlcList'); box.innerHTML = '';
  for (const st of E.dlcStores(state.emu.guest)) {
    const used = st.slots.filter(x => x.name).length;
    const d = document.createElement('div'); d.className = 'dlcbox';
    const h = document.createElement('h4'); h.textContent = t('dlcK' + st.kind) + ' ' + used + '/' + st.slots.length; d.appendChild(h);
    for (const x of st.slots) {
      if (!x.name) continue;
      const r = document.createElement('div'); r.className = 'dlcrow';
      const n = document.createElement('span'); n.textContent = x.name;
      const b = document.createElement('button'); b.className = 'btn small'; b.textContent = t('dlcDel');
      b.onclick = () => { state.emu.guest = E.dlcFree(state.emu.guest, st.kind, x.slot); toast(t('dlcGone', { n: x.name })); renderEmu(); };
      r.append(n, b); d.appendChild(r);
    }
    const f = document.createElement('div'); f.className = 'dlcrow free'; f.textContent = t('dlcFreeN', { n: st.slots.length - used }); d.appendChild(f);
    box.appendChild(d);
  }
}
$('btnDlc').onclick = () => $('fileDlc').click();
$('fileDlc').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; if (!files.length || !state.emu) return;
  for (const f of files) {
    try {
      const rec = E.dlcRecord(new Uint8Array(await f.arrayBuffer()));
      const r = E.dlcAdd(state.emu.guest, rec);
      state.emu.guest = r.guest;
      toast(t('dlcOk', { n: E.dlcName(rec), k: t('dlcK' + r.store.kind), s: r.slot + 1 }));
    } catch (err) { toast(t('dlcBad', { f: f.name, e: em(err) }), true); }
  }
  renderEmu();
};
$('btnEmu').onclick = () => $('fileEmu').click();
$('fileEmu').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  const g = new Uint8Array(await f.arrayBuffer());
  try { const dev = E.emuDetect(g); const pk = E.packGuest(g, dev); state.emu = { guest: g, dev, size: pk.blob.length }; state.emuRemove = false; state.doom = null; renderDoom(); }
  catch (err) { toast(t('emuBad', { e: em(err) }), true); }
  renderEmu();
};
$('btnEmuOff').onclick = () => { state.emu = null; state.emuRemove = true; renderEmu(); };

// ---------------------------------------------------------------- DOOM
function keySel(id, val, wheel) {
  const sel = $(id); sel.innerHTML = '';
  [[1, 'A'], [2, 'B'], [4, 'C']].concat(wheel ? [[8, t('kWheel')]] : []).forEach(([v, l]) => { const o = document.createElement('option'); o.value = v; o.textContent = l; sel.appendChild(o); });
  sel.value = String(val);
}
function renderDoom() {
  if (!state.ctx) return;
  let s;
  const d = state.doom, has = doomInstalled();
  if (!D.embedded()) s = t('doomMissing');
  else if (d && !d.ok.length) s = t('doomBig');
  else if (d) s = t('doomOk', { eps: 'E' + d.ok.join(', E') }) + '\n' + t('doomUse');
  else if (has && state.emuRemove) s = t('doomGone');
  else if (has) s = t('doomHas') + '\n' + t('doomUse');
  else s = t('doomNone');
  const sel = $('doomEp'), cur = sel.value; sel.innerHTML = '';
  (d && d.ok.length ? d.ok : ['1']).forEach(e => { const o = document.createElement('option'); o.value = e; o.textContent = 'E' + e; sel.appendChild(o); });
  if (cur && [...sel.options].some(o => o.value === cur)) sel.value = cur;
  const f = d && d.fit[sel.value];
  const mus = $('doomMusic');
  mus.disabled = !f || !f.music; if (f && !f.music) mus.checked = false;
  if (f && !f.music) s += '\n' + t('doomNoMusic');
  const room = appRoom();
  $('btnWad').disabled = !room;
  $('btnDoomOff').hidden = !has || state.emuRemove || !!d;
  if (!room) s = t('noRoomApp');
  else if (!d && (state.emu || (emuInstalled() && !state.emuRemove))) s += '\n' + t('doomVsEmu');
  $('doomStat').textContent = s;
  // Keys: for a new WAD or an installed DOOM
  const showKeys = !!d || (has && !state.emuRemove);
  $('doomKeys').hidden = !showKeys;
  if (showKeys) {
    if (!state.doomKeys) state.doomKeys = Object.assign({}, (has && D.doomKeysGet(state.ctx.rom)) || D.KEYS_DEFAULT);
    const k = state.doomKeys, old = has && !d && !D.doomKeysGet(state.ctx.rom);
    keySel('kFire', k.fire); keySel('kFwd', k.fwd); keySel('kBack', k.back); keySel('kUse', k.use, true); keySel('kMod', k.mod);
    $('kRun').checked = k.run; $('kInv').checked = k.invert;
    ['kFire', 'kFwd', 'kBack', 'kUse', 'kMod', 'kRun', 'kInv'].forEach(id => { $(id).disabled = old; });
    const err = D.doomKeysCheck(k);
    $('keysStat').textContent = old ? t('kOld') : (err ? em(err) : t('kFixed'));
    $('keysStat').classList.toggle('bad', !!err && !old);
  }
  renderPlan();
}
function keysChanged() {
  const k = state.doomKeys; if (!k) return;
  k.fire = +$('kFire').value; k.fwd = +$('kFwd').value; k.back = +$('kBack').value; k.use = +$('kUse').value; k.mod = +$('kMod').value;
  k.run = $('kRun').checked; k.invert = $('kInv').checked;
  renderDoom();
}
['kFire', 'kFwd', 'kBack', 'kUse', 'kMod', 'kRun', 'kInv'].forEach(id => { $(id).onchange = keysChanged; });
$('btnDoomOff').onclick = () => { state.doom = null; state.emu = null; state.emuRemove = true; renderEmu(); renderDoom(); };
$('doomEp').onchange = () => renderDoom();
$('btnWad').onclick = () => $('fileWad').click();
$('fileWad').onchange = (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  busy(async () => {
    const w = new Uint8Array(await f.arrayBuffer());
    try {
      const emb = D.embedded(); if (!emb) throw new Error(t('doomMissing'));
      const eps = D.wadEpisodes(w).filter(x => x !== '4'); if (!eps.length) throw new Error('E1 bis E3 fehlen');
      // Does the episode fit in the ROM? With music, otherwise without.
      const room = D.REGION_OFF - D.WAD_OFF - 4, fit = {}, ok = [];
      for (const ep of eps) {
        const size = async (m) => { const sub = D.wadSub(w, ep, m); return (await D.convert(sub, D.gbaExtras(sub), emb.wasm)).length; };
        const sm = await size(true), music = sm <= room, sp = music ? sm : await size(false), plain = sp <= room;
        fit[ep] = { music, plain, sizeM: sm, sizeP: sp }; if (plain) ok.push(ep);
      }
      state.doom = { wad: w, eps, fit, ok }; state.emu = null; state.emuRemove = false; renderEmu();
    } catch (err) { state.doom = null; toast(t('doomBad', { e: em(err) }), true); }
    renderDoom();
  });
};

// ------------------------------------------------------------- actions
$('btnOpen').onclick = () => $('fileRom').click();
$('fileRom').onchange = (e) => { const f = e.target.files[0]; e.target.value = ''; if (f) busy(() => loadRom(f)); };
const drop = $('drop');
drop.onclick = () => $('fileRom').click();
drop.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('fileRom').click(); } };
['dragenter', 'dragover'].forEach(ev => document.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add('over'); }));
['dragleave', 'drop'].forEach(ev => document.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.remove('over'); }));
document.addEventListener('drop', (e) => {
  const files = [...(e.dataTransfer?.files || [])]; if (!files.length) return;
  const rom = files.find(f => f.size === 16777216 || /\.bin$/i.test(f.name) && f.size > 4194304);
  if (rom) busy(() => loadRom(rom));
  else if (state.ctx) busy(() => uploadFiles(files));
});
$('btnUpload').onclick = $('btnUpload2').onclick = () => $('fileSheets').click();
$('fileSheets').onchange = (e) => { const fs = [...e.target.files]; e.target.value = ''; if (fs.length) busy(() => uploadFiles(fs)); };
$('btnZipFigs').onclick = () => busy(() => zipSheets('fig'));
$('btnZipAll').onclick = () => busy(() => zipSheets('spr'));
$('btnNumbers').onclick = () => busy(async () => {
  for (const g of state.ctx.groups) {
    const big = C.numberImage(String(g), 64, 64, 4), small = C.numberImage(String(g), 32, 32, 2);
    [Array(20).fill(big), null, null, Array(24).fill(small), null, null].forEach((subs, k) => {
      const i = g + k, orig = state.ctx.entries[i];
      state.changed.set(i, subs ? C.spriteEncode(orig, subs, C.partOptions(k)) : C.spriteBlank(orig, C.partOptions(k)));
    });
    refreshTile('fig', g);
  }
  toast(t('numOk'));
});
// ------------------------------------------------------------ field select
function renderFeld() {
  if (!state.ctx) return;
  const fe = F.embedded(), rom = state.ctx.rom, ed = F.edition(rom), can = F.canFields(rom);
  const box = $('feldOn'), bag = $('bagOn');
  box.checked = !!state.feld && can; box.disabled = !fe || !can;
  bag.checked = !!state.bag; bag.disabled = !fe || !ed;
  let s;
  // six fields (Tropical/Glacier): row visible there only
  const si = S6.sixInfo(rom), sixIn = !!si, sixOld = !!si && si.version < S6.VERSION, sixCan = !!S6.kindOf(rom), jade = S6.kindOf(rom) === 'jade';
  $('sixRow').hidden = !sixCan;
  for (const [id, k] of [['sixOnTxt', jade ? 'sixOnJ' : 'sixOn'], ['sixHintTxt', jade ? 'sixHintJ' : 'sixHint']]) { $(id).dataset.t = k; $(id).textContent = t(k); }
  $('sixOn').checked = sixIn || !!state.six || !!state.sixWant; $('sixOn').disabled = (sixIn && !sixOld) || !fe || !S6.embedded();
  $('btnSix').hidden = jade || (sixIn && !sixOld && si.planet === state.sixPlanet);
  $('sixPlanetRow').hidden = !sixCan || !(sixIn || state.six || state.sixWant);
  $('sixPlanet').checked = state.sixPlanet !== false; $('sixPlanet').disabled = !fe || !S6.embedded();
  if (!fe) s = t('feldMissing');
  else if (!ed) s = t('feldUnknown');
  else {
    s = t('feldCur', { f: F.fieldNow(rom) });
    if (can) {
      s += ' ' + t('feldList', { s: F.fieldNames(rom).concat(!sixIn && (state.six || (jade && state.sixWant)) ? (jade ? ['Sky'] : ['Land', 'Water', 'Sky']) : []).join(', ') });
      const sl = F.feldSlots(rom, fe.stub).map(x => F.NAMES[x.field]);
      if (sl.length) s += ' ' + t('feldStored', { s: sl.join(', ') });
    } else s += '\n' + t('feldNo');
    if (sixCan) {
      if (jade) { if (sixOld) s += '\n' + t('sixOldJ'); else if (sixIn) s += '\n' + t('sixInJ'); else if (!S6.sixCan(rom)) s += '\n' + t('sixNoFw'); }
      else if (sixOld && state.six) s += '\n' + t('sixOk', { n: state.sixName });
      else if (sixOld) s += '\n' + t('sixOld');
      else if (sixIn && si.planet !== state.sixPlanet && !state.six) s += '\n' + t('sixPlanetNeed');
      else if (sixIn) s += '\n' + t('sixIn');
      else if (!S6.sixCan(rom)) s += '\n' + t('sixNoFw');
      else if (state.sixWant && !state.six) s += '\n' + t('sixNeed');
      else if (state.six) s += '\n' + t('sixOk', { n: state.sixName });
    }
    if (state.feld || state.bag) s += '\n' + t('feldUse');
  }
  $('feldStat').textContent = s;
  renderPlan();
}
$('feldOn').onchange = (e) => { state.feld = e.target.checked; renderFeld(); };
$('sixOn').onchange = (e) => {
  state.sixWant = e.target.checked;
  if (!e.target.checked) state.six = null;
  else { state.feld = true; if (!state.six && S6.sixNeedsSource(state.ctx.rom)) $('fileSix').click(); }
  renderFeld();
};
$('btnSix').onclick = () => $('fileSix').click();
$('sixPlanet').onchange = (e) => { state.sixPlanet = e.target.checked; renderFeld(); };
$('fileSix').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  const buf = new Uint8Array(await f.arrayBuffer());
  if (!S6.sixSource(buf)) { toast(t('sixBad', { n: f.name }), true); renderFeld(); return; }
  state.six = buf; state.sixName = f.name; state.sixWant = true; state.feld = true;
  toast(t('sixOk', { n: f.name }));
  renderFeld();
};
$('bagOn').onchange = (e) => { state.bag = e.target.checked; renderFeld(); };

/* Build the new ROM from the loaded one and everything chosen in the tabs.
   Returns { rom, name, res } or null (the reason is shown). same: nothing chosen. */
async function composeRom() {
  const keysDirty = !state.doom && doomInstalled() && state.doomKeys && D.doomKeysGet(state.ctx.rom) && JSON.stringify(state.doomKeys) !== JSON.stringify(D.doomKeysGet(state.ctx.rom));
  const si0 = S6.sixInfo(state.ctx.rom), sixUp = !!si0 && (si0.version < S6.VERSION || si0.planet !== state.sixPlanet) && !S6.sixNeedsSource(state.ctx.rom);
  const emuUp = emuUpPending();
  if (!state.changed.size && !state.emu && !state.emuRemove && !state.doom && !keysDirty && !emuUp && !state.six && !sixUp && !(state.sixWant && !S6.sixInfo(state.ctx.rom)) && state.feld === state.feldInit && state.bag === state.bagInit && !state.feldStale) return { rom: state.ctx.rom, name: state.name, res: null, same: true };
  const fe = F.embedded(), feldSaved = fe ? F.feldSlots(state.ctx.rom, fe.stub) : [];
  if (state.emu && state.doom) { toast(t('doomBoth'), true); return null; }
  if (state.doom && state.doomKeys && D.doomKeysCheck(state.doomKeys)) { toast(em(D.doomKeysCheck(state.doomKeys)), true); return null; }
  let rom = state.ctx.rom, res = null;
  if (state.changed.size) {
    try { res = C.buildRom(state.ctx, state.changed, currentDataPack()); rom = res.rom; }
    catch (e) { toast(t('tooBig', { e: em(e) }), true); return null; }
  }
  if (fe) rom = F.feldStrip(rom, { stub: fe.stub, oldStub: E.embedded() && E.embedded().stub }, E);
  // Emulator instead of installed DOOM: remove DOOM completely first (program part, WAD), then install the emulator
  if (state.emu && doomInstalled()) { try { rom = E.emuRemove(rom); } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; } }
  if (state.emu || state.emuRemove) {
    const emb = E.embedded();
    try {
      if (state.emu) { if (!emb) throw new Error(t('emuMissing')); rom = E.emuPatch(rom, state.emu.guest, emb.app, emb.stub).rom; }
      else rom = E.emuRemove(rom);
    } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; }
  }
  // newer emulator program: only the program changes, the Tamagotchi's save stays
  if (emuUp && F.appKind(rom) === 2) { const u = E.emuUpgrade(rom, E.embedded().app); if (u) rom = u; }
  if (state.doom && !state.doom.ok.length) { toast(t('doomBig'), true); return null; }
  if (state.doom) {
    const emb = D.embedded(), embE = E.embedded();
    try {
      if (!emb || !embE) throw new Error(t('doomMissing'));
      rom = (await D.doomBuild(rom, state.doom.wad, { episode: $('doomEp').value, music: $('doomMusic').checked, keys: state.doomKeys || D.KEYS_DEFAULT }, { app: emb.app, xip: emb.xip, wasm: emb.wasm, stub: embE.stub }, E)).rom;
    } catch (e) { toast(t('doomFail', { e: em(e) }), true); return null; }
  }
  // Change the keys of an already installed DOOM
  if (!state.doom && doomInstalled() && !state.emuRemove && state.doomKeys && D.doomKeysGet(rom)) {
    const k = state.doomKeys, err = D.doomKeysCheck(k);
    if (err) { toast(em(err), true); return null; }
    rom = D.doomKeysSet(rom, k);
  }
  const needSrc = S6.sixNeedsSource(rom);
  if (state.sixWant && needSrc && !state.six && !S6.sixInfo(rom)) { toast(t('sixNeed'), true); return null; }
  const sixHave = S6.sixInfo(rom);
  if (sixHave && needSrc && !state.six && sixHave.version >= S6.VERSION && sixHave.planet !== state.sixPlanet) { toast(t('sixPlanetNeed'), true); return null; }
  if ((state.six || (state.sixWant && !needSrc) || (!needSrc && sixHave)) && !(sixHave && sixHave.version >= S6.VERSION && sixHave.planet === state.sixPlanet)) {
    if (state.doom || doomInstalled() || state.emu) { toast(t('sixFail', { e: t('sixApp') }), true); return null; }
    try { rom = S6.sixApply(rom, state.six, S6.embedded(), { planet: state.sixPlanet !== false }); }
    catch (e) { toast(t('sixFail', { e: em(e) }), true); return null; }
  }
  if (S6.sixInfo(rom)) rom = S6.sixFix(rom);   // the archive may have moved: rain pictures point to 0xF00000
  if (fe && (state.feld || state.bag)) {
    try { rom = F.feldApply(rom, { fields: state.feld, bag: state.bag }, feldSaved, { feld: fe.feld, stub: fe.stub }, E); }
    catch (e) { toast(t('feldFail', { e: em(e) }), true); return null; }
  }
  const name = baseName() + (state.changed.size ? '_paramix' : '') + (state.emu ? '_emu' : '') + (state.doom ? '_doom_E' + $('doomEp').value : '') + (S6.sixInfo(rom) ? (S6.kindOf(rom) === 'jade' ? '_4fields' : '_6fields') : state.feld ? '_fields' : '') + (state.bag ? '_items' : '') + '.bin';
  return { rom, name, res };
}
$('btnSave').onclick = () => busy(async () => {
  const c = await composeRom(); if (!c) return;
  if (c.same) { toast(t('nothing')); return; }
  download(c.rom, c.name);
  state.saved = state.changed.size;
  toast(c.res ? t('saved', { name: c.name, n: state.changed.size, kb: c.res.free / 1024 | 0 }) : t('savedEmu', { name: c.name }));
});

// ------------------------------------------------------------- device (UART)
const FL = window.ParamixFlash;
const dv = { io: null, d: null, loaded: false, run: false, cancel: false };
function devRender(msg) {
  const ser = !!(navigator.serial && FL.embedded());
  $('btnConn').disabled = !ser || dv.run;
  $('btnDump').disabled = !dv.d || dv.run;
  $('btnWrite').disabled = !dv.d || dv.run || !state.ctx;
  $('devOnly').disabled = $('devKeep').disabled = dv.run;
  $('btnDevStop').hidden = !dv.run;
  $('devInfo').textContent = dv.d ? t('devOn', { ed: dv.d.info.name, mb: dv.d.info.capacity >> 20, l: dv.loaded ? t('devLoaded') : '' }) : t('devNone');
  if (msg !== undefined) $('devStat').textContent = msg;
  else if (!ser) $('devStat').textContent = t('devNoSerial');
  else if (dv.d && !state.ctx && !dv.run) $('devStat').textContent = t('devNeedRom');
}
/* Flasher errors and notes carry a code; German has its own texts, English uses the module's */
function flMsg(x) { return x && x.code && lang !== 'en' && T[lang]['fl_' + x.code] ? t('fl_' + x.code, x.p) : (x.message || x.text || String(x)); }
function devProg(ph, done, total) {
  $('devBar').style.width = (total ? Math.min(100, done / total * 100) : 0).toFixed(1) + '%';
  const big = total >= 0x400000, u = big ? 1 << 20 : 1024;
  const n = ph === 'write' ? done + ' / ' + total : (done / u | 0) + ' / ' + (total / u | 0) + (big ? ' MB' : ' KB');
  $('devStat').textContent = t('ph' + ph) + '  ' + n;
}
async function devTask(fn) {
  dv.run = true; dv.cancel = false; devRender('');
  let msg;
  try { await fn(); msg = $('devStat').textContent; }
  catch (e) { msg = dv.cancel ? t('devCancelled') : t('devErr', { e: flMsg(e) }); }
  dv.run = false; devRender(msg);
}
$('btnDevice').onclick = () => { $('device').hidden = !$('device').hidden; devRender(); };
$('btnDevStop').onclick = () => { dv.cancel = true; };
$('btnConn').onclick = async () => {
  let port;
  try { port = await navigator.serial.requestPort(); } catch (e) { return; }
  devTask(async () => {
    if (dv.io) { await dv.io.close(); dv.io = null; dv.d = null; }
    dv.io = await FL.serialOpen(port);
    const r = await FL.connect(dv.io, FL.embedded(), {}, devProg);
    dv.d = r.dev; dv.loaded = r.loaded;
    devRender(t('devReady'));
  });
};
$('btnDump').onclick = () => devTask(async () => {
  const img = await dv.d.dump(devProg, () => dv.cancel);
  const st = new Date().toISOString().slice(0, 19).replace(/[-:]/g, '').replace('T', '-');
  const name = 'paradise-' + dv.d.info.name.toLowerCase().replace(/ /g, '-') + '-' + st + '.bin';
  download(img, name);
  await loadRom(new File([img], name));
  devRender(t('devDumped', { n: name }));
});
$('btnWrite').onclick = async () => {
  const c = await busy(composeRom); if (!c) return;
  const keep = $('devKeep').checked;
  if (!confirm(t('devSure')) || (!keep && !confirm(t('devSureFull')))) return;
  devTask(async () => {
    const fe = F.embedded();
    const plan = keep ? FL.keepPlan(F, E, fe && { stub: fe.stub }, { emuChanged: !!state.emu }) : null;
    const r = await dv.d.write(c.rom, { onlyChanged: $('devOnly').checked, keep: plan }, devProg, () => dv.cancel);
    let m = r.written ? t('devDone', { w: r.written, k: r.kept ? t('devKept', { k: r.kept }) : '' }) : t('devSame');
    if (r.notes.length) m += '\n' + r.notes.map(flMsg).join('\n');
    devRender(m);
  });
};

window.addEventListener('beforeunload', (e) => { if (state.ctx && state.changed.size && state.saved !== state.changed.size) { e.preventDefault(); e.returnValue = t('leave'); } });

applyLang(); devRender();
window.__paramix = state; // for tests
})();
