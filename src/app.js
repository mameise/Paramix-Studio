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
    startTitle: 'Mehr aus deinem Tamagotchi Paradise',
    startText: 'Paramix Studio baut dein eigenes Paradise-ROM um. Lade dein Paradise-Abbild (16 MB Flash-Dump) oder lies es direkt vom Gerät aus, wähl aus, was dazukommen soll, und schreib das neue ROM zurück.',
    featHead: 'Was du damit machen kannst',
    g1t: 'Emulatoren: Farb-Tamas, Tama-Go, Friends und Meets', g1: 'Tamagotchi iD L, Plus Color, P\'s oder 4U+ laufen mit TamaEmu auf dem Paradise, der Tama-Go mit T7Emu samt Figuren, dort auch der Tamagotchi Friends (Test), Meets und On mit T18Emu (Test). Mit Spielstand und Uhr. Die ROMs bringst du selbst mit.',
    g2t: 'DOOM', g2: 'DOOM spielen auf dem Paradise, mit Musik und eigener Tastenbelegung. Die WAD bringst du selbst mit.',
    g3t: 'Mehr Felder', g3: 'Tropical und Glacier bekommen Land, Water und Sky dazu, Jade Forest bekommt Sky. Mit Bildern, Spezialfiguren und passendem Planeten.',
    g4t: 'Menü auf dem Gerät', g4: 'Felder wechseln, Items in die Tasche legen und Tamapoints setzen, direkt am Paradise mit A und C.',
    g5t: 'Gerät auslesen und beschreiben', g5: 'Mit einem USB-UART-Dongle das ROM sichern und das neue zurückschreiben. Spielstände bleiben, nur Geändertes wird geschrieben. Spielstände auch einzeln sichern und zurückspielen.',
    g6t: 'Eigene Figuren', g6: 'Jede Figur als Bild-Sheet zum Bearbeiten, auch Digimon von DIM-Karten. Farben und Augen werden automatisch angepasst.',
    dropTitle: 'ROM hierher ziehen oder klicken',
    dropText: 'Land, Water, Sky, Jade Forest, Tropical und Glacier werden erkannt.',
    f1t: 'Bleibt bei dir', f1: 'Das ROM wird nirgends hochgeladen. Alles läuft in diesem Browserfenster, auch ohne Internet.',
    f2t: 'Nichts Fremdes dabei', f2: 'Paramix Studio enthält kein ROM, keine Firmware und keine WAD. Alles kommt von deinen eigenen Geräten und Dateien.',
    f3t: 'Geprüft', f3: 'Prüfsummen werden neu gerechnet, geänderte Bilder vor dem Speichern noch einmal gelesen und jeder geschriebene Sektor auf dem Gerät gegengelesen.',
    planTitle: 'Dein neues ROM', planFig: 'Figuren', planEmu: 'Emulator', planDoom: 'DOOM', planMenu: 'Menü', planApps: 'Weitere Emulatoren',
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
    help: 'Hilfe', helpTitle: 'So geht\'s', h1t: 'Ablauf', h1: '1. ROM öffnen (Datei vom eigenen Paradise) oder mit "Gerät" direkt auslesen.\n2. In den Tabs wählen, was hinein soll: Figuren, Emulator, DOOM, Menü.\n3. Speichern oder mit "Gerät" aufs Paradise schreiben. Mit "Spielstände behalten" bleibt der Spielstand auf dem Gerät.', h2t: 'Tasten am Paradise (TamaEmu, T7Emu, T18Emu)', hk1: 'A und C', hv1: 'auf dem Heimbildschirm: Emulator oder Menü starten', hk2: 'Rad lange', hv2: 'Stand speichern und zurück ins Paradise', hk3: 'Rad kurz', hv3: 'T7Emu: Einstellungen und Figur', hk4: 'Rad in den ersten 3 s', hv4: 'gespeicherten Stand verwerfen (mit A bestätigen), der nächste Start beginnt frisch ab dem eigenen Spielstand des Tamas', hk5: 'B beim Einschalten', hv5: 'mit Autostart: Paradise statt Emulator', hk6: 'Reset-Knopf', hv6: 'mit Autostart: der Emulator startet wieder',  h3t: 'Uhr', h3: 'Mit "Uhr beim Start vom Paradise übernehmen" (Tab Emulator) stellt der Emulator bei jedem Start seine Uhr auf die Zeit des Paradise. Was dazwischen war, wird nicht nachgespielt. Die Uhr also am Paradise stellen.', h4t: 'Wieder reingehen', h4: 'Der Emulator macht genau dort weiter, wo du mit dem Rad rausgegangen bist. Der eigene Spielstand des Tamas bleibt auch dann, wenn der gespeicherte Stand verworfen wird.', h5t: 'Menü und Felder', h5: 'A und C auf dem Heimbildschirm. Rad wählt, B bestätigt, C geht zurück. Beim Feldwechsel wird der Spielstand des alten Felds gepackt und der des neuen zurückgeholt.', h6t: 'Spielstände sichern', h6: 'Tab Spielstand oder Gerät, "Spielstand sichern". Vor einem Wechsel des Emulators sichern, der alte Spielstand geht dabei verloren.', device: 'Gerät', devTitle: 'Gerät über UART', devHint: 'UART-Dongle mit 3,3 V an die drei Kontakte: links RX vom Dongle, Mitte GND, rechts TX vom Dongle. Am Paradise den Download-Bildschirm öffnen (Planet, A, Connect, Download, Download), dann Verbinden. Braucht Chrome oder Edge am Computer.',
    devConnect: 'Verbinden', devDump: 'ROM auslesen', devWrite: 'Neues ROM aufs Gerät schreiben', devOnly: 'Nur Änderungen schreiben', devKeep: 'Spielstände behalten', devStop: 'Abbrechen',
    devNoSerial: 'Dieser Browser kann nicht mit dem Dongle reden. Bitte Chrome oder Edge am Computer verwenden.', devNone: 'Nicht verbunden.',
    devOn: 'Verbunden: {ed}, {mb} MB{l}', devLoaded: ', Flash-App geladen', devReady: 'Bereit.', devNeedRom: 'Zum Schreiben zuerst ein ROM öffnen (oder auslesen).',
    phload: 'Flash-App laden', phread: 'Auslesen', phcompare: 'Vergleichen', phwrite: 'Schreiben', phcheck: 'Prüfen', phprepare: 'Vorbereiten',
    devDumped: 'ROM ausgelesen und als {n} gespeichert. Es ist jetzt im Studio geöffnet.',
    devSure: 'Das neue ROM wird jetzt aufs Gerät geschrieben. Gerät während des Schreibens eingeschaltet lassen. Auf eigene Gefahr schreiben?',
    devSureFull: 'Ohne "Spielstände behalten" wird auch der Spielstand mit dem aus dem ROM überschrieben. Trotzdem schreiben?',
    devEmuLost: 'Auf dem Gerät ist {o} mit seinem Spielstand. Das neue ROM hat {n}, der Spielstand von {o} geht deshalb verloren. Wer ihn behalten will, sichert ihn vorher unter "Spielstand". Trotzdem schreiben?', devEmuNone: 'keinen Emulator', devEmuNew: '{o} mit einer neu gewählten Firmware',
    devDone: 'Fertig und geprüft: {w} Sektoren geschrieben{k}. Das Gerät kann neu gestartet werden (Reset oder Batterie).', devKept: ', {k} Sektoren Spielstand behalten',
    devSame: 'Nichts zu schreiben, das Gerät hat schon dieses ROM.', devErr: 'Fehler: {e}', devCancelled: 'Abgebrochen.',
    fl_timeout: 'keine Antwort vom Gerät ({c} {a})', fl_noAck: 'das Gerät antwortet nicht. Steht es auf dem Download-Bildschirm?', fl_repeats: 'zu viele Wiederholungen beim Laden',
    fl_devCancel: 'das Gerät hat das Laden abgebrochen', fl_unexpected: 'unerwartete Antwort: {r}', fl_hello: 'unerwartete Antwort auf HELLO', fl_notReady: 'Flash nicht bereit',
    fl_readErr: 'Lesefehler bei {a}', fl_noCrc: 'keine Prüfsumme bei {a}', fl_cancelled: 'abgebrochen', fl_size: 'das Abbild muss genau 16 MB gross sein',
    fl_notParadise: 'kein Paradise-Abbild (SONIXDEV fehlt)', fl_cancelledWrite: 'abgebrochen. Gerät eingeschaltet lassen und nochmals schreiben',
    fl_notAccepted: 'Sektor {a} nicht angenommen. Gerät eingeschaltet lassen und nochmals schreiben', fl_differs: 'Sektor {a} weicht nach dem Schreiben ab. Gerät eingeschaltet lassen und nochmals schreiben',
    fl_checkFail: 'Prüfung bei {a} falsch. Gerät eingeschaltet lassen und nochmals schreiben', fl_noAnswer: 'keine Antwort. Gerät auf den Download-Bildschirm stellen und nochmals versuchen', fl_lost: 'das Gerät antwortet vor dem Schreiben nicht mehr. Geschrieben wurde nichts. Paradise aus- und einschalten, Download-Bildschirm, neu verbinden und nochmals schreiben',
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
    emuPick: 'Firmware oder ROM wählen', emuOff: 'Emulator entfernen',
    emuHint: 'Baut einen Emulator ins ROM. TamaEmu für Farb-Tamas (iD L, Plus Color, P\'s, 4U+): Firmware mit 4 MB oder 8 MB. T7Emu für den Tama-Go (V7-ROM mit 640 KB, dazu Figuren) und den Tamagotchi Friends (ROM mit 768 KB, Test). T18Emu für Meets und On: TG18-Firmware mit 8 MB. Die Dateien bringst du selbst mit.', t7Ok: 'V7-ROM erkannt (T7Emu). Es wird beim Speichern eingebaut.', t7OkFr: 'Tamagotchi Friends erkannt (T7Emu, Test). Es wird beim Speichern eingebaut. Figuren gibt es dafür keine.', t7Has: 'In diesem ROM ist T7Emu eingebaut.', t7HasFr: 'In diesem ROM ist T7Emu mit Tamagotchi Friends eingebaut.', t7Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Rad kurz: Einstellungen und Figur. Hängt T7Emu: mit A und C starten, loslassen und in den ersten 3 Sekunden das Rad drücken und mit A bestätigen.', t7FigPick: 'Figuren hinzufügen', t7FigNone: 'Noch keine Figuren.', t7FigBad: '{f}: keine Figur.', t7FigFull: 'Kein Platz mehr für {f}.', t7FigMax: 'Kein Platz für weitere Figuren.', t7FigDup: '{f} ist schon dabei.', appPick: 'Weiteren Emulator dazu', appHint: 'Noch ein Tama-Go- oder Friends-ROM (T7Emu). Start über das Menü (A und C), dafür muss im Tab Menü etwas eingeschaltet sein.', appNone: 'Keine weiteren Emulatoren.', appBad: '{f}: kein Tama-Go- oder Friends-ROM.', appMax: 'Höchstens 6 weitere Emulatoren.', appNeedMenu: 'Weitere Emulatoren starten über das Menü: im Tab Menü Feldwechsel oder Items einschalten.', appFail: 'Weitere Emulatoren: {e}', appSix: 'Sechs Felder gehen nicht zusammen mit weiteren Emulatoren.', t7SaveBtn: 'T7Emu-Spielstand (.bin)', saveFor: 'T7Emu', saveMain: 'Haupt-Emulator', t7SaveXDone: 'Spielstand für {n} übernommen. Jetzt das ROM speichern oder aufs Gerät schreiben.', t7NoSave: 'Das Tama hat noch keinen Spielstand.', t7SaveInDone: 'T7Emu-Spielstand übernommen. Jetzt das ROM speichern oder aufs Gerät schreiben.', t7SaveNeed: 'Dafür muss T7Emu im ROM sein.', saveEmuAny: 'Emulator-Spielstand (.sav / .bin)',
    emuUse: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Hängt TamaEmu: mit A und C starten, loslassen und in den ersten 3 Sekunden das Rad drücken und mit A bestätigen. Dann beginnt er neu ab dem eigenen Spielstand des Tamas.',
    emuNone: 'Kein Emulator gewählt.', tabSave: 'Spielstand', saveParaOut: 'Paradise-Spielstand (.tamasave)', saveEmuOut: 'TamaEmu-Spielstand (.sav)', saveInfo: 'Exportieren holt die Spielstände aus diesem ROM. Ein Emulator-Spielstand (TamaEmu .sav oder T7Emu .bin) kommt ins ROM, danach das ROM speichern oder aufs Gerät schreiben. Ein Paradise-Spielstand geht direkt aufs Gerät, dafür oben das Gerät verbinden.', saveOutT: 'Exportieren', saveInT: 'Einspielen', saveEmuInDone: 'TamaEmu-Spielstand übernommen ({d}). Jetzt das ROM speichern oder aufs Gerät schreiben.', saveEmuOther: 'Im ROM ist TamaEmu für {a}, der Spielstand ist für {b}. Trotzdem übernehmen?', saveNoRoom: 'Für TamaEmu ist in diesem ROM kein Platz.', saveEmuBad: 'Das ist kein Emulator-Spielstand (TamaEmu .sav oder T7Emu .bin).', saveNoEmu: 'In diesem ROM ist kein Emulator.', saveDone: '{n} gespeichert.', saveNoClock: '{n} gespeichert. Die Uhr ist nicht dabei, in der App eventuell neu stellen.', devSaveRead: 'Spielstand sichern', devSaveWrite: 'Spielstand aufs Gerät', devSaveHint: 'Sichern holt den Paradise-Spielstand mit Uhr und den Spielstand von TamaEmu oder T7Emu. Aufs Gerät geht nur der Paradise-Spielstand.', devSaveSure: 'Der Spielstand wird auf das Gerät ({ed}) geschrieben und ersetzt den jetzigen. Weiter?', devSaveDone: 'Fertig: {w} Seiten geschrieben. {c} Das Gerät kann neu gestartet werden.', devSaveClock: 'Uhr übernommen.', devSaveClockNo: 'Die Uhr bitte am Gerät stellen.', devSaveGot: 'Gesichert: {n}.', fl_saveOtherEd: 'der Spielstand ist von einer anderen Ausgabe ({s}), das Gerät ist {d}', fl_saveFamily: 'der Spielstand passt nicht zu dieser Ausgabe', fl_saveNoSettings: 'im Spielstand fehlt der Einstellungsblock, das ist kein vollständiger Paradise-Spielstand', fl_saveOutside: '{n} Seiten liegen ausserhalb des Spielstands und werden nicht geschrieben', fl_saveIncomplete: 'der Spielstand enthält nicht alle Seiten, fehlende bleiben wie auf dem Gerät', emuClock: 'Uhr beim Start vom Paradise übernehmen', emuClockHint: 'Bei jedem Start stellt der Emulator seine Uhr auf die Zeit des Paradise. Die Zeit dazwischen wird nicht nachgespielt. Ohne Haken läuft es wie bisher.', emuAuto: 'Beim Einschalten starten', emuAutoHint: 'Rad lange drücken: zurück ins Paradise, es bleibt dort. Wieder in den Emulator: A und C oder Reset-Knopf.', emuHas: 'In diesem ROM ist ein Emulator eingebaut.', emuBig: 'Diese Firmware ist gross: der Feldwechsel im Menü hat daneben keinen Platz.', emuUp: 'Beim Speichern kommt die neue Emulator-Version hinein. Mit "Spielstände behalten" bleibt der Spielstand.',
    emuOk: 'Firmware erkannt ({size}). Sie wird beim Speichern eingebaut.', emuBad: 'Firmware nicht erkannt: {e}', t18Ok: 'TG18-Firmware erkannt (T18Emu, Meets/On). Sie wird beim Speichern eingebaut.', t18Has: 'In diesem ROM ist T18Emu eingebaut.', t18Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Hängt T18Emu: mit A und C starten, loslassen, in den ersten 3 Sekunden das Rad drücken und mit A bestätigen. Dann startet die Firmware neu ab ihrem eigenen Spielstand. Experimentell: das Tama läuft noch langsamer als echt.',
    emuGone: 'Emulator wird beim Speichern entfernt.', emuMissing: 'In dieser Ausgabe fehlt das Emulator-Programm.',
    emuFail: 'Emulator nicht eingebaut: {e}', savedEmu: 'Gespeichert: {name}',
    dlcPick: 'Download-Inhalte hinzufügen', dlcHint: 'Essen, Kleider, Accessoires, Spiele usw. als .jpg-Download-Dateien. Mehrere Dateien auf einmal gehen, bei P\'s einen VDP-Satz immer ganz.', dlcFreeN: '{n} frei', dlcDel: 'entfernen', dlcK0: 'Mahlzeiten', dlcK1: 'Snacks', dlcK2: 'Kleider', dlcK3: 'Accessoires', dlcK4: 'Gegenstände', dlcK5: 'Alltagsdinge', dlcK6: 'Hintergründe', dlcK7: 'Spiele', dlcK8: 'Ausflugsziele', dlcK10: 'VDP-Rezepte', dlcK15: 'Dekorationen', dlcK16: 'Gutscheine', dlcK17: 'Bingo', dlcK18: 'Gashapon', dlcK19: 'Figuren', dlcOk: '{n} liegt jetzt in {k}, Platz {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} entfernt.',
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
    startTitle: 'Get more out of your Tamagotchi Paradise',
    startText: 'Paramix Studio rebuilds your own Paradise ROM. Load your Paradise dump (16 MB flash image) or read it straight from the device, pick what to add, and write the new ROM back.',
    featHead: 'What you can do',
    g1t: 'Emulators: colour tamas, Tama-Go, Friends and Meets', g1: 'Tamagotchi iD L, Plus Color, P\'s or 4U+ run on the Paradise with TamaEmu, the Tama-Go with T7Emu and its figures, the Tamagotchi Friends there too (test), Meets and On with T18Emu (test). With save and clock. Bring your own ROMs.',
    g2t: 'DOOM', g2: 'Play DOOM on the Paradise, with music and your own key layout. Bring your own WAD.',
    g3t: 'More fields', g3: 'Tropical and Glacier get Land, Water and Sky, Jade Forest gets Sky. With pictures, special characters and the matching planet.',
    g4t: 'Menu on the device', g4: 'Switch fields, put items in the bag and set Tamapoints, right on the Paradise with A and C.',
    g5t: 'Read and write the device', g5: 'Back up the ROM and write the new one with a USB UART dongle. Saves are kept, only changes are written. Saves can also be backed up and restored on their own.',
    g6t: 'Your own characters', g6: 'Every character as an image sheet to edit, Digimon from DIM cards too. Colours and eyes are fitted automatically.',
    dropTitle: 'Drop a ROM here or click',
    dropText: 'Land, Water, Sky, Jade Forest, Tropical and Glacier are recognised.',
    f1t: 'Stays on your computer', f1: 'The ROM is never uploaded. Everything runs in this browser tab, even offline.',
    f2t: 'Nothing borrowed inside', f2: 'Paramix Studio contains no ROM, no firmware and no WAD. Everything comes from your own devices and files.',
    f3t: 'Checked', f3: 'Checksums are recalculated, changed images are read back before saving and every sector written to the device is verified.',
    planTitle: 'Your new ROM', planFig: 'Characters', planEmu: 'Emulator', planDoom: 'DOOM', planMenu: 'Menu', planApps: 'More emulators',
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
    help: 'Help', helpTitle: 'How it works', h1t: 'Steps', h1: '1. Open a ROM (file of your own Paradise) or read it straight from the device with "Device".\n2. Choose in the tabs what goes in: figures, emulator, DOOM, menu.\n3. Save, or write it to the Paradise with "Device". With "Keep saves" the save on the device stays.', h2t: 'Buttons on the Paradise (TamaEmu, T7Emu, T18Emu)', hk1: 'A and C', hv1: 'on the home screen: start the emulator or the menu', hk2: 'Wheel long', hv2: 'save the state and go back to the Paradise', hk3: 'Wheel short', hv3: 'T7Emu: settings and figure', hk4: 'Wheel in the first 3 s', hv4: 'drop the saved state (confirm with A), the next start begins fresh from the Tama\'s own save', hk5: 'B when switching on', hv5: 'with autostart: Paradise instead of the emulator', hk6: 'Reset button', hv6: 'with autostart: the emulator starts again',  h3t: 'Clock', h3: 'With "Take the clock from the Paradise at the start" (tab Emulator) the emulator sets its clock to the Paradise\'s time at every start. The time in between is not played through. So set the clock on the Paradise.', h4t: 'Coming back', h4: 'The emulator continues exactly where you left with the wheel. The Tama\'s own save stays even when the saved state is dropped.', h5t: 'Menu and fields', h5: 'A and C on the home screen. The wheel selects, B confirms, C goes back. Switching the field packs the save of the old field and brings back the one of the new field.', h6t: 'Backing up saves', h6: 'Tab Save, or Device, "Back up save". Back up before changing the emulator, the old save is lost then.', device: 'Device', devTitle: 'Device over UART', devHint: 'UART dongle with 3.3 V on the three contacts: left RX from the dongle, middle GND, right TX from the dongle. Open the download screen on the Paradise (Planet, A, Connect, Download, Download), then Connect. Needs Chrome or Edge on a computer.',
    devConnect: 'Connect', devDump: 'Read ROM', devWrite: 'Write new ROM to the device', devOnly: 'Only write changes', devKeep: 'Keep saves', devStop: 'Cancel',
    devNoSerial: 'This browser cannot talk to the dongle. Please use Chrome or Edge on a computer.', devNone: 'Not connected.',
    devOn: 'Connected: {ed}, {mb} MB{l}', devLoaded: ', flash app loaded', devReady: 'Ready.', devNeedRom: 'To write, open a ROM first (or read one).',
    phload: 'Loading flash app', phread: 'Reading', phcompare: 'Comparing', phwrite: 'Writing', phcheck: 'Checking', phprepare: 'Preparing',
    devDumped: 'ROM read and saved as {n}. It is now open in the Studio.',
    devSure: 'The new ROM is written to the device now. Keep the device switched on while writing. Write at your own risk?',
    devSureFull: 'Without "Keep saves" the save on the device is replaced by the one in the ROM. Write anyway?',
    devEmuLost: 'The device has {o} with its save. The new ROM has {n}, so the save of {o} is lost. To keep it, back it up first under "Save". Write anyway?', devEmuNone: 'no emulator', devEmuNew: '{o} with a newly chosen firmware',
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
    emuPick: 'Choose firmware or ROM', emuOff: 'Remove emulator',
    emuHint: 'Builds an emulator into the ROM. TamaEmu for colour tamas (iD L, Plus Color, P\'s, 4U+): firmware of 4 MB or 8 MB. T7Emu for the Tama-Go (V7 ROM of 640 KB, plus figures) and the Tamagotchi Friends (ROM of 768 KB, test). T18Emu for Meets and On: TG18 firmware of 8 MB. Bring your own files.', t7Ok: 'V7 ROM recognised (T7Emu). It is built in on save.', t7OkFr: 'Tamagotchi Friends recognised (T7Emu, test). It is built in on save. There are no figures for it.', t7Has: 'This ROM contains T7Emu.', t7HasFr: 'This ROM contains T7Emu with Tamagotchi Friends.', t7Use: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). Wheel short: settings and figure. If T7Emu hangs: start with A and C, release them and press the wheel within the first 3 seconds and confirm with A.', t7FigPick: 'Add figures', t7FigNone: 'No figures yet.', t7FigBad: '{f}: not a figure.', t7FigFull: 'No room left for {f}.', t7FigMax: 'No room for more figures.', t7FigDup: '{f} is already in.', appPick: 'Add another emulator', appHint: 'One more Tama-Go or Friends ROM (T7Emu). Started from the menu (A and C), so something in the Menu tab must be switched on.', appNone: 'No other emulators.', appBad: '{f}: not a Tama-Go or Friends ROM.', appMax: 'At most 6 more emulators.', appNeedMenu: 'More emulators start from the menu: switch on the field switch or items in the Menu tab.', appFail: 'More emulators: {e}', appSix: 'Six fields do not go together with more emulators.', t7SaveBtn: 'T7Emu save (.bin)', saveFor: 'T7Emu', saveMain: 'Main emulator', t7SaveXDone: 'Save for {n} taken over. Now save the ROM or write it to the device.', t7NoSave: 'The Tama has no save yet.', t7SaveInDone: 'T7Emu save taken over. Now save the ROM or write it to the device.', t7SaveNeed: 'T7Emu has to be in the ROM for that.', saveEmuAny: 'Emulator save (.sav / .bin)',
    emuUse: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). If TamaEmu hangs: start with A and C, release them and press the wheel within the first 3 seconds and confirm with A. It then starts over from the save the Tama keeps itself.',
    emuNone: 'No emulator chosen.', tabSave: 'Save', saveParaOut: 'Paradise save (.tamasave)', saveEmuOut: 'TamaEmu save (.sav)', saveInfo: 'Export takes the saves out of this ROM. An emulator save (TamaEmu .sav or T7Emu .bin) goes into the ROM, then save the ROM or write it to the device. A Paradise save goes straight to the device, connect it above for that.', saveOutT: 'Export', saveInT: 'Import', saveEmuInDone: 'TamaEmu save taken over ({d}). Now save the ROM or write it to the device.', saveEmuOther: 'The ROM has TamaEmu for {a}, the save is for {b}. Take it anyway?', saveNoRoom: 'This ROM has no room for TamaEmu.', saveEmuBad: 'This is not an emulator save (TamaEmu .sav or T7Emu .bin).', saveNoEmu: 'This ROM has no emulator.', saveDone: '{n} saved.', saveNoClock: '{n} saved. The clock is not included, set it in the app if needed.', devSaveRead: 'Back up save', devSaveWrite: 'Save to device', devSaveHint: 'Back up takes the Paradise save with the clock and the save of TamaEmu or T7Emu. Only the Paradise save goes to the device.', devSaveSure: 'The save is written to the device ({ed}) and replaces the current one. Continue?', devSaveDone: 'Done: {w} pages written. {c} The device can be restarted.', devSaveClock: 'Clock taken over.', devSaveClockNo: 'Please set the clock on the device.', devSaveGot: 'Backed up: {n}.', fl_saveOtherEd: 'the save is from another edition ({s}), the device is {d}', fl_saveFamily: 'the save does not fit this edition', fl_saveNoSettings: 'the settings block is missing, this is not a complete Paradise save', fl_saveOutside: '{n} pages lie outside the save and are not written', fl_saveIncomplete: 'the save does not hold every page, missing ones stay as on the device', emuClock: 'Take the clock from the Paradise at the start', emuClockHint: 'At every start the emulator sets its clock to the Paradise\'s time. The time in between is not played through. Unticked it works as before.', emuAuto: 'Start when switched on', emuAutoHint: 'Hold the wheel: back to the Paradise, it stays there. Back into the emulator: A and C, or the reset button.', emuHas: 'This ROM contains an emulator.', emuBig: 'This firmware is big: the field switch of the menu has no room next to it.', emuUp: 'Saving puts in the new emulator version. With "Keep saves" the save stays.',
    emuOk: 'Firmware recognised ({size}). It is built in on save.', emuBad: 'Firmware not recognised: {e}', t18Ok: 'TG18 firmware recognised (T18Emu, Meets/On). It is built in on save.', t18Has: 'This ROM contains T18Emu.', t18Use: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). If T18Emu hangs: start with A and C, release them, press the wheel within the first 3 seconds and confirm with A. The firmware then starts over from its own save. Experimental: the Tama still runs slower than the real one.',
    emuGone: 'The emulator is removed on save.', emuMissing: 'This build does not contain the emulator program.',
    emuFail: 'Emulator not built in: {e}', savedEmu: 'Saved: {name}',
    dlcPick: 'Add download content', dlcHint: 'Food, clothes, accessories, games etc. as .jpg download files. Several files at once work, on the P\'s always a whole VDP set.', dlcFreeN: '{n} free', dlcDel: 'remove', dlcK0: 'Meals', dlcK1: 'Snacks', dlcK2: 'Clothing', dlcK3: 'Accessories', dlcK4: 'Items', dlcK5: 'Daily items', dlcK6: 'Wallpapers', dlcK7: 'Games', dlcK8: 'Destinations', dlcK10: 'VDP recipes', dlcK15: 'Decorations', dlcK16: 'Coupons', dlcK17: 'Bingo', dlcK18: 'Gashapon', dlcK19: 'Characters', dlcOk: '{n} now sits in {k}, slot {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} removed.',
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
  [/^no room for the spare pages$/, 'kein Platz für die Ersatzseiten'],
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
  [/^this content fits no store of this firmware$/, 'dieser Inhalt passt in kein Fach dieser Firmware'],
  [/^content too big for its store$/, 'Inhalt zu gross für sein Fach'],
  [/^only for the 8 MB firmware$/, 'nur für 8-MB-Firmware'],
  [/^no matching store$/, 'kein passendes Fach'],
  [/^already added$/, 'schon drin'],
  [/^(.+): all slots used, remove one first$/, (m, a) => storeDe(a) + ': alle Plätze belegt, zuerst einen entfernen'],
  [/^no such slot$/, 'kein solcher Platz'],
  [/^not a TG18 firmware \(8 MB\)$/, 'keine TG18-Firmware (8 MB)'],
  [/^not enough room: the firmware needs (\d+) blocks of 64 KB, (\d+) are free$/, 'zu wenig Platz: die Firmware braucht $1 Blöcke zu 64 KB, frei sind $2'],
  [/^T18Emu program incomplete \(flash part missing\)$/, 'T18Emu-Programm unvollständig (Flash-Teil fehlt)'],
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
  [/^no room for the field saves next to this emulator firmware$/, 'neben dieser Emulator-Firmware haben die Spielstände der Felder keinen Platz'],
  [/^the character data reaches (\S+), the emulator needs 0x830000 onwards$/, 'die Figuren-Daten reichen bis $1, der Emulator braucht den Platz ab 0x830000'],
  [/^the stored saves do not fit into the room for the fields$/, 'die gespeicherten Spielstände passen nicht in den Platz für die Felder'],
  [/^script table full$/, 'Skripttabelle voll'],
  [/^unexpected Land planet table$/, 'unerwartete Planetentabelle im Land-ROM'],
  [/^not a Paradise save \(\.tamasave\)$/, 'kein Paradise-Spielstand (.tamasave)'],
  [/^unknown save version (\S+)$/, 'unbekannte Spielstand-Version $1'],
  [/^save is cut off$/, 'der Spielstand ist abgeschnitten'],
];
function storeDe(label) { const k = ['Meals', 'Snacks', 'Clothing', 'Accessories', 'Items', 'Daily items', 'Wallpapers', 'Games', 'Destinations', '', 'VDP recipes', '', '', '', '', 'Decorations', 'Coupon', 'Bingo', 'Gashapon', 'Characters'].indexOf(label); return k < 0 ? label : t('dlcK' + k); }
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
  if (state.ctx) {
    renderInfo(); renderDim(); renderFieldPick(); renderEmu(); renderDoom(); renderFeld(); renderSave();
    document.querySelectorAll('#gridFigs .tile').forEach((b) => { const g = +b.dataset.idx; b.querySelector('.cap span').textContent = [state.charNames.get(g), groupRole(g)].filter(Boolean).join(' '); });
    if (state.open) openDrawer(state.open.kind, state.open.idx);
  }
  try { if (!dv.run) devRender(); } catch (e) {}   // dv does not exist yet on the first call
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
  state.emu = null; state.t7save = null; state.t7saveX = null; state.emuRemove = false; state.doom = null; state.doomKeys = null; state.six = null; state.sixName = ''; state.sixWant = false;
  { const si = S6.sixInfo(buf); state.sixPlanet = si && si.version >= 5 ? si.planet : true; }
  { const l = stubList(buf); const g = E.autoGet(buf, l); state.emuAuto = g === null ? E.autoGetOld(buf, l) : g; }
  { const c = E.emuClockGet(buf); state.emuClock = c === null ? true : c; }
  { const fe = F.embedded(), f = fe && F.feldFind(buf, fe.stub);
    state.feld = !!(f && (f.flags & F.F_FIELDS)); state.bag = !!(f && (f.flags & F.F_BAG));
    state.feldInit = state.feld; state.bagInit = state.bag;
    state.extras = f ? E.appsRead(buf, F.feldApps(buf, fe.stub).apps).map((x) => ({ rom: x.rom })) : []; state.extrasDirty = false;
    // A menu from an older Studio version is rebuilt on save (e.g. 0.9.5 had no item pictures)
    state.feldStale = false;
    if (f) {
      try {
        const embOld = { stub: fe.stub, oldStub: E.embedded() && E.embedded().stub };
        const fa = F.feldApps(buf, fe.stub), low = fa.apps.length ? Math.min(...fa.apps.map((a) => a.src - 0x10000000 + 0x11000)) : 0;
        const re = F.feldApply(F.feldStrip(buf, embOld, E), { fields: state.feld, bag: state.bag, apps: fa.apps, name0: fa.name0, appsLow: low }, F.feldSlots(buf, fe.stub), { feld: fe.feld, stub: fe.stub }, E);
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
  renderInfo(); renderGrids(); renderDim(); renderEmu(); renderDoom(); renderFeld(); renderSave(); if (!dv.run) devRender();
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
  ['figs', 'all', 'dim', 'emu', 'doom', 'feld', 'save'].forEach(k => $('pane-' + k).hidden = k !== b.dataset.tab);
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
    [t('planEmu'), (emuUpPending() || emuAutoDirty()) ? t('stUpd') : st(emuOn, !!state.emu, kind === 2, state.emu ? E.DEVICES[state.emu.dev].label : ''), state.emu || emuUpPending() || emuAutoDirty() ? 'new' : emuOn ? 'keep' : kind === 2 ? 'del' : 'off'],
    [t('planDoom'), st(doomOn, !!state.doom, kind === 1, state.doom ? 'E' + $('doomEp').value + ($('doomMusic').checked ? ' + ' + t('doomMusic') : '') : ''), state.doom ? 'new' : doomOn ? 'keep' : kind === 1 ? 'del' : 'off'],
  ];
  const menuOn = state.feld || state.bag, parts = [state.feld ? t('feldOn') : '', state.bag ? t('bagOn') : ''].filter(Boolean).join(', ');
  chips.push([t('planMenu'), menuOn ? parts + (state.feldStale && state.feld === state.feldInit && state.bag === state.bagInit ? ' (' + t('stUpd') + ')' : '') : (menuOld ? t('stDel') : t('stNone')), menuOn ? ((state.feld === state.feldInit && state.bag === state.bagInit && !state.feldStale) ? 'keep' : 'new') : menuOld ? 'del' : 'off']);
  if ((state.extras && state.extras.length) || state.extrasDirty) chips.push([t('planApps'), (state.extras || []).map((x) => E.t7XName(x.rom)).join(', ') || t('stNone'), state.extrasDirty ? 'new' : 'keep']);
  const sixIn = !!S6.sixInfo(rom);
  if (sixIn || state.six || state.sixWant) chips.push([t('planSix'), sixIn ? t('stKeep') : t('stNew'), sixIn ? 'keep' : 'new']);
  // Flash: from 0x111000 to the end
  const base = 0x111000, end = 0x1000000, total = end - base, segs = [];
  const assets = estimateEnd() - base + (state.six && !sixIn ? 0xB9000 : 0); segs.push(['fw', t('segFw'), assets]);
  let doomSize = 0, emuSize = 0;
  if (state.doom) { const f = state.doom.fit[$('doomEp').value]; doomSize = (f ? ($('doomMusic').checked && f.music ? f.sizeM : f.sizeP) : 0) + (0xD48000 - 0xCA0000); }
  else if (doomOn) doomSize = (rom[0x830000] | rom[0x830001] << 8 | rom[0x830002] << 16 | rom[0x830003] << 24) + (0xD48000 - 0xCA0000);
  if (state.emu) emuSize = state.emu.size || 0;
  else if (emuOn) { const c7 = E.t7Cfg(rom), c18 = E.t18Cfg(rom); if (c18) emuSize = c18.map.filter((b) => b !== 0xFFFF).length << 16; else if (c7) emuSize = c7.romLen + c7.figs.reduce((n, f) => n + f.len, 0); else { const o = 0xA0000 + 0x140 + 12; emuSize = rom[o] | rom[o + 1] << 8 | rom[o + 2] << 16 | rom[o + 3] << 24; } }
  for (const x of state.extras || []) emuSize += 0x20000 + ((x.rom.length + 0xFFFF) & ~0xFFFF);   // more emulators: program, save, ROM
  if (doomOn) segs.push(['doom', t('segDoom'), doomSize]);
  if (emuOn || (state.extras && state.extras.length)) segs.push(['emu', t('segEmu'), emuSize]);
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
/* Where the stub can sit: the emulator's own at 0x0B0000, or the menu's (also elsewhere) */
function stubList(rom) {
  const ee = E.embedded(), fe = F.embedded(), l = [];
  if (ee) l.push([E.STUB_OFF, ee.stub]);
  if (fe) { l.push([E.STUB_OFF, fe.stub]); const f = F.feldFind(rom, fe.stub); if (f && f.stubOff !== E.STUB_OFF) l.push([f.stubOff, fe.stub]); }
  return l;
}
function emuAutoDirty() { return emuInstalled() && !state.emu && !state.emuRemove && !state.doom && (!!E.autoGet(state.ctx.rom, stubList(state.ctx.rom)) !== !!state.emuAuto || emuClockDirty()); }
function emuClockDirty() { const c = E.emuClockGet(state.ctx.rom); return c !== null && c !== !!state.emuClock; }
function emuUpPending() {
  const emb = E.embedded();
  if (state.emu || state.emuRemove || state.doom || !emb || !emuInstalled()) return false;
  try {
    if (F.emuType(state.ctx.rom) === 't7') return !!(E.t7Embedded() && E.t7Upgrade(state.ctx.rom, E.t7Embedded()));
    if (F.emuType(state.ctx.rom) === 't18') return !!(E.t18Embedded() && E.t18Upgrade(state.ctx.rom, E.t18Embedded(), E.embedded() && E.embedded().stub));
    return !!E.emuUpgrade(state.ctx.rom, emb.app);
  } catch (e) { return false; }
}
/* Room for emulator and DOOM: both need the area after the characters from 0x830000 and the program space at 0x0A0000 */
function appRoom() { const r = state.ctx.rom; return estimateEnd() <= 0x830000 && (F.appKind(r) || r.subarray(0xA0000, 0xA1000).every(b => b === 0xFF)); }
function renderEmu() {
  if (!state.ctx) return;
  const has = emuInstalled();
  $('btnEmuOff').hidden = !has || state.emuRemove;
  let s;
  if (!E.embedded()) s = t('emuMissing');
  const t7In = has && F.emuType(state.ctx.rom) === 't7', t7 = state.emu ? !!state.emu.t7 : t7In && !state.emuRemove;
  const t18In = has && F.emuType(state.ctx.rom) === 't18', t18 = state.emu ? !!state.emu.t18 : t18In && !state.emuRemove;
  const t7Fr = state.emu ? !!state.emu.t7 && E.t7Kind(state.emu.rom) === 'friends' : t7In && !!E.t7Cfg(state.ctx.rom) && E.t7Cfg(state.ctx.rom).romLen === 0xC0000;
  if (state.emu) s = state.emu.t7 ? t(t7Fr ? 't7OkFr' : 't7Ok') : state.emu.t18 ? t('t18Ok') : t('emuOk', { size: E.DEVICES[state.emu.dev].label });
  else if (state.emuRemove) s = t('emuGone');
  else s = has ? t(t7In ? (t7Fr ? 't7HasFr' : 't7Has') : t18In ? 't18Has' : 'emuHas') + (emuUpPending() ? ' ' + t('emuUp') : '') : t('emuNone');
  s += (state.emu || (has && !state.emuRemove) ? '\n' + t(t7 ? 't7Use' : t18 ? 't18Use' : 'emuUse') : '');
  const bigIn = has && !state.emu && !state.emuRemove && state.ctx.rom[0xA0148] === 0 && state.ctx.rom[0xA014A] === 0x84;
  if ((state.emu && state.emu.big) || bigIn) s += '\n' + t('emuBig');
  const room = appRoom();
  $('btnEmu').disabled = !room;
  if (!room) s = t('noRoomApp');
  else if (!state.emu && (state.doom || (doomInstalled() && !state.emuRemove))) s += '\n' + t('emuVsDoom');
  $('emuStat').textContent = s;
  renderT7Figs(t7 && room && !t7Fr);
  renderApps();
  $('emuAutoRow').hidden = !(state.emu || (has && !state.emuRemove)) || !!state.doom;
  $('emuClockRow').hidden = $('emuAutoRow').hidden;
  $('emuAuto').checked = !!state.emuAuto;
  $('emuClock').checked = state.emuClock !== false;
  renderPlan();
  const big = state.emu && ['idl', 'ps', '4u', '4u-plain'].includes(state.emu.dev);   // 1.6.6: P's and 4U as well
  $('emuDlc').hidden = !big;
  if (big) renderDlc();
}
/* T7Emu figures: list with remove buttons */
function t7State() {
  if (state.emu && state.emu.t7) return state.emu;
  const cur = E.t7FromRom(state.ctx.rom); if (!cur) return null;
  return { t7: true, dev: 't7', rom: cur.rom, figs: cur.figs, size: 0 };
}
function t7Size(st) { return st.rom.length + st.figs.reduce((n, f) => n + f.data.length, 0); }
function renderT7Figs(on) {
  $('btnT7Fig').hidden = !on; $('t7Figs').hidden = !on;
  const box = $('t7Figs'); box.innerHTML = '';
  if (!on) return;
  const st = t7State(); if (!st) return;
  if (!st.figs.length) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = t('t7FigNone'); box.appendChild(d); return; }
  st.figs.forEach((f, i) => {
    const c = document.createElement('span'); c.className = 'chip keep'; c.textContent = E.t7FigName(f.name) + ' ';
    const x = document.createElement('button'); x.className = 'btn small'; x.textContent = '×';
    x.onclick = () => { const s2 = t7State(); s2.figs.splice(i, 1); s2.size = t7Size(s2); state.emu = s2; state.emuRemove = false; renderEmu(); };
    c.appendChild(x); box.appendChild(c);
  });
}
/* More emulators (1.6.0): T7 ROMs started from the menu */
function renderApps() {
  const box = $('appsList'); box.innerHTML = '';
  const room = appRoom();
  $('btnApp').disabled = !room || !E.t7Embedded();
  const l = state.extras || [];
  if (!l.length) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = t('appNone'); box.appendChild(d); return; }
  l.forEach((x, i) => {
    const c = document.createElement('span'); c.className = 'chip keep'; c.textContent = E.t7XName(x.rom) + ' ';
    const b = document.createElement('button'); b.className = 'btn small'; b.textContent = '×';
    b.onclick = () => { state.extras.splice(i, 1); state.extrasDirty = true; state.t7saveX = null; renderEmu(); };
    c.appendChild(b); box.appendChild(c);
  });
  if (!(state.feld || state.bag)) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = ' ' + t('appNeedMenu'); box.appendChild(d); }
}
$('btnApp').onclick = () => $('fileApp').click();
$('fileApp').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  const g = new Uint8Array(await f.arrayBuffer());
  if (!E.t7Detect(g)) { toast(t('appBad', { f: f.name }), true); return; }
  state.extras = state.extras || [];
  if (state.extras.length >= E.APPX_MAX) { toast(t('appMax'), true); return; }
  state.extras.push({ rom: g }); state.extrasDirty = true; state.t7saveX = null; renderEmu();
};
$('btnT7Fig').onclick = () => $('fileT7Fig').click();
$('fileT7Fig').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; if (!files.length) return;
  const st = t7State(); if (!st) return;
  for (const f of files) {
    const raw = new Uint8Array(await f.arrayBuffer());
    if (!E.t7FigDetect(raw)) { toast(t('t7FigBad', { f: f.name }), true); continue; }
    const d = E.t7FigData(raw);
    if (st.figs.length >= 16) { toast(t('t7FigMax'), true); break; }
    if (st.figs.some((x) => E.t7FigName(x.name) === E.t7FigName(f.name))) { toast(t('t7FigDup', { f: E.t7FigName(f.name) }), true); continue; }
    if (E.t7Room(st.figs.concat([{ name: f.name, data: d }]), st.rom.length) < 0) { toast(t('t7FigFull', { f: E.t7FigName(f.name) }), true); continue; }
    st.figs.push({ name: f.name, data: d });
  }
  st.size = t7Size(st); state.emu = st; state.emuRemove = false; state.doom = null;
  renderDoom(); renderEmu();
};
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
      const rec = E.dlcRecord(new Uint8Array(await f.arrayBuffer()), state.emu.dev);
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
  if (E.t18Detect(g)) {
    let n = 0; for (let i = 0; i < 128; i++) if (!g.subarray(i << 16, (i + 1) << 16).every((v) => v === 0xFF)) n++;
    state.emu = { t18: true, dev: 't18', rom: g, size: n << 16 }; state.emuRemove = false; state.doom = null; renderDoom();
  } else if (E.t7Detect(g)) {
    const old = state.emu && state.emu.t7 ? state.emu : t7State();   // the figures stay
    const st = { t7: true, dev: 't7', rom: g, figs: old && E.t7Kind(g) === 'go' ? old.figs : [] };
    st.size = t7Size(st); state.emu = st; state.emuRemove = false; state.doom = null; renderDoom();
  } else
  try { const dev = E.emuDetect(g); const pk = E.packGuest(g, dev); state.emu = { guest: g, dev, size: pk.blob.length, big: pk.big }; state.emuRemove = false; state.doom = null; renderDoom(); }
  catch (err) { toast(t('emuBad', { e: em(err) }), true); }
  renderEmu();
};
$('emuAuto').onchange = (e) => { state.emuAuto = e.target.checked; renderEmu(); };
$('emuClock').onchange = (e) => { state.emuClock = e.target.checked; renderEmu(); };
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
  if (!state.changed.size && !state.t7save && !state.emu && !state.emuRemove && !state.doom && !keysDirty && !emuUp && !emuAutoDirty() && !state.six && !sixUp && !(state.sixWant && !S6.sixInfo(state.ctx.rom)) && state.feld === state.feldInit && state.bag === state.bagInit && !state.feldStale && !state.extrasDirty && !state.t7saveX) return { rom: state.ctx.rom, name: state.name, res: null, same: true };
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
      if (state.emu && state.emu.t7) { if (!emb || !E.t7Embedded()) throw new Error(t('emuMissing')); rom = E.t7Patch(rom, state.emu.rom, state.emu.figs, E.t7Embedded(), emb.stub, { lang }); }
      else if (state.emu && state.emu.t18) { if (!emb || !E.t18Embedded()) throw new Error(t('emuMissing')); rom = E.t18Patch(rom, state.emu.rom, E.t18Embedded(), emb.stub, { lang }).rom; }
      else if (state.emu) { if (!emb) throw new Error(t('emuMissing')); rom = E.emuPatch(rom, state.emu.guest, emb.app, emb.stub, { lang }).rom; }
      else rom = E.emuRemove(rom);
    } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; }
  }
  if (state.t7save && F.emuType(rom) === 't7') rom = SV.t7SaveIn(rom, state.t7save);
  // newer emulator program: only the program changes, the Tamagotchi's save stays
  if (emuUp && F.appKind(rom) === 2) { const ty = F.emuType(rom), u = ty === 't7' ? E.t7Upgrade(rom, E.t7Embedded()) : ty === 't18' ? E.t18Upgrade(rom, E.t18Embedded(), E.embedded() && E.embedded().stub) : E.emuUpgrade(rom, E.embedded().app); if (u) rom = u; }
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
    if (state.extras && state.extras.length) { toast(t('appSix'), true); return null; }
    try { rom = S6.sixApply(rom, state.six, S6.embedded(), { planet: state.sixPlanet !== false }); }
    catch (e) { toast(t('sixFail', { e: em(e) }), true); return null; }
  }
  if (S6.sixInfo(rom)) rom = S6.sixFix(rom);   // the archive may have moved: rain pictures point to 0xF00000
  // more emulators (1.6.0): the old blocks go, the list is placed again from the top of the emulator area
  let apps = [], appsLow = 0;
  if (fe) { const old = E.appsRead(state.ctx.rom, F.feldApps(state.ctx.rom, fe.stub).apps); if (old.length) rom = E.appsClear(rom, old); }
  if (state.extras && state.extras.length) {
    if (!fe || !(state.feld || state.bag)) { toast(t('appNeedMenu'), true); return null; }
    if (!E.t7Embedded()) { toast(t('emuMissing'), true); return null; }
    try { const r = E.appsPlace(rom, state.extras, E.t7Embedded(), { lang, clock: state.emuClock !== false }); rom = r.rom; apps = r.list; appsLow = r.low;
      if (state.t7saveX && apps[state.t7saveX.i]) rom = SV.t7SaveIn(rom, state.t7saveX.save, apps[state.t7saveX.i].off + 0x10000); }
    catch (e) { toast(t('appFail', { e: em(e) }), true); return null; }
  }
  const name0 = F.emuType(rom) === 't7' && apps.length ? E.t7XName(E.t7FromRom(rom).rom) : '';
  if (fe && (state.feld || state.bag)) {
    try { rom = F.feldApply(rom, { fields: state.feld, bag: state.bag, apps, name0, appsLow }, feldSaved, { feld: fe.feld, stub: fe.stub }, E); }
    catch (e) { toast(t('feldFail', { e: em(e) }), true); return null; }
  }
  // autostart of the emulator (the stubs above were written with it off), clock switch (1.5.1)
  if (F.appKind(rom) === 2) rom = E.emuClockSet(rom, state.emuClock !== false);
  if (F.appKind(rom) === 2) {
    let a = E.autoSet(rom, stubList(rom), !!state.emuAuto);
    const fe2 = F.embedded();
    // stub of an older Studio (no switch): put in the current one
    if (!a && state.emuAuto && E.embedded() && !(fe2 && F.feldFind(rom, fe2.stub))) {
      try { rom = E.installApp(rom, rom.slice(0xA0000, 0xB0000), E.embedded().stub); a = E.autoSet(rom, stubList(rom), true); } catch (e) { a = null; }
    }
    if (a) rom = a;
  }
  const name = baseName() + (state.changed.size ? '_paramix' : '') + (state.emu ? (state.emu.t7 ? '_t7' : state.emu.t18 ? '_t18' : '_emu') : '') + (state.doom ? '_doom_E' + $('doomEp').value : '') + (S6.sixInfo(rom) ? (S6.kindOf(rom) === 'jade' ? '_4fields' : '_6fields') : state.feld ? '_fields' : '') + (state.bag ? '_items' : '') + '.bin';
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
  $('btnSaveRead').disabled = $('btnSaveWrite').disabled = $('btnParaIn').disabled = !dv.d || dv.run;
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
$('btnHelp').onclick = () => { $('helpBox').hidden = !$('helpBox').hidden; };
$('helpClose').onclick = () => { $('helpBox').hidden = true; };
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
  // an emulator save on the device that the new ROM cannot keep: say so before (1.5.1)
  if (keep) {
    const EMU = { 0x43584D50: 'TamaEmu', 0x37584D50: 'T7Emu', 0x38584D50: 'T18Emu' };
    let dm = 0;
    try { const p = await dv.d.read(0xA0000, 0x1000); dm = (p[0x140] | p[0x141] << 8 | p[0x142] << 16 | p[0x143] << 24) >>> 0; } catch (e) { dm = 0; }
    const im = (c.rom[0xA0140] | c.rom[0xA0141] << 8 | c.rom[0xA0142] << 16 | c.rom[0xA0143] << 24) >>> 0;
    if (EMU[dm] && (dm !== im || state.emu)) {
      const n = EMU[im] ? (dm === im ? t('devEmuNew', { o: EMU[im] }) : EMU[im]) : t('devEmuNone');
      if (!confirm(t('devEmuLost', { o: EMU[dm], n }))) return;
    }
  }
  devTask(async () => {
    const fe = F.embedded();
    const plan = keep ? FL.keepPlan(F, E, fe && { stub: fe.stub }, { emuChanged: !!state.emu, t7Save: !!state.t7save, t7SaveX: state.t7saveX ? state.t7saveX.i : -1 }) : null;
    let img = c.rom;
    if (F.emuType(img) === 't7') { const ck = await dv.d.clockRead(); if (ck && (ck.ctrl & 0x80)) img = E.t7Wall(img, ck.counter, new Date()); }
    if (F.emuType(img) === 't18') { const ck = await dv.d.clockRead(); if (ck && (ck.ctrl & 0x80)) img = E.t18Wall(img, ck.counter, new Date()); }   // T18Emu: local time for the first start   // T7Emu: local time for the first time setting
    const r = await dv.d.write(img, { onlyChanged: $('devOnly').checked, keep: plan }, devProg, () => dv.cancel);
    let m = r.written ? t('devDone', { w: r.written, k: r.kept ? t('devKept', { k: r.kept }) : '' }) : t('devSame');
    if (r.notes.length) m += '\n' + r.notes.map(flMsg).join('\n');
    devRender(m);
  });
};

// ------------------------------------------------------------- save games
const SV = window.ParamixSaves;
const stamp = () => { const d = new Date(), z = (n) => String(n).padStart(2, '0'); return d.getFullYear() + z(d.getMonth() + 1) + z(d.getDate()) + '-' + z(d.getHours()) + z(d.getMinutes()); };
const edSlug = (id) => (FL.EDITIONS[id] || 'paradise').toLowerCase().replace(/ /g, '-');
function saveMsg(x) { return t('fl_' + x.code, x.p); }
function romEmuCfg(rom) { return F.appKind(rom) === 2 && F.emuType(rom) === 'tama' ? SV.emuCfg(rom.subarray(0xA0000, 0xA1000)) : null; }
function romT7(rom) { return F.emuType(rom) === 't7'; }
/* Further T7Emus of the loaded ROM (1.6.0): [{ name, saveOff }] */
function romExtras(rom) { const fe = F.embedded(); return fe ? E.appsRead(rom, F.feldApps(rom, fe.stub).apps) : []; }
/* Which T7Emu the save buttons are for: -1 the main one, otherwise the further one with this index */
function saveFor() { const v = $('saveFor').value; return v === '' || v === undefined ? -1 : +v; }
function renderSave() {
  if (!state.ctx) return;
  const xs = romExtras(state.ctx.rom), sel = $('saveFor'), cur = sel.value;
  sel.innerHTML = '';
  if (romT7(state.ctx.rom)) { const o = document.createElement('option'); o.value = '-1'; o.textContent = t('saveMain') + ' (' + E.t7XName(E.t7FromRom(state.ctx.rom).rom) + ')'; sel.appendChild(o); }
  xs.forEach((x, i) => { const o = document.createElement('option'); o.value = String(i); o.textContent = x.name; sel.appendChild(o); });
  if ([...sel.options].some((o) => o.value === cur)) sel.value = cur;
  $('saveForRow').hidden = !xs.length;
  const t7 = romT7(state.ctx.rom) || xs.length > 0;
  for (const id of ['btnEmuOut', 'btnEmuIn']) { $(id).dataset.t = t7 ? 't7SaveBtn' : romEmuCfg(state.ctx.rom) ? 'saveEmuOut' : 'saveEmuAny'; $(id).textContent = t($(id).dataset.t); }
  $('btnEmuOut').disabled = !romEmuCfg(state.ctx.rom) && !(t7 && t7SaveOutSel());
  $('saveStat').textContent = t('saveInfo');
  let con = false; try { con = !!dv.d && !dv.run; } catch (e) {}
  $('btnParaIn').disabled = !con;
}
function t7SaveOutSel() {
  const i = saveFor();
  if (i < 0) return romT7(state.ctx.rom) ? SV.t7SaveOut(state.ctx.rom) : null;
  const x = romExtras(state.ctx.rom)[i]; return x ? SV.t7SaveOut(state.ctx.rom, x.saveOff) : null;
}
$('saveFor').onchange = () => renderSave();
$('btnParaIn').onclick = () => $('btnSaveWrite').click();
$('btnEmuIn').onclick = () => $('fileEmuSav').click();
$('fileEmuSav').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  const g = new Uint8Array(await f.arrayBuffer());
  if (SV.t7SaveCheck(g)) {
    const i = saveFor(), xs = romExtras(state.ctx.rom);
    if (i >= 0 && xs[i]) { state.t7saveX = { i, save: g }; renderPlan(); $('saveStat').textContent = t('t7SaveXDone', { n: xs[i].name }); return; }
    if (!romT7(state.ctx.rom)) { toast(t('t7SaveNeed'), true); return; }
    state.t7save = g; renderPlan();
    $('saveStat').textContent = t('t7SaveInDone');
    return;
  }
  let dev, pk;
  try { dev = E.emuDetect(g); pk = E.packGuest(g, dev); } catch (err) { toast(t('saveEmuBad'), true); return; }
  if (!appRoom()) { toast(t('saveNoRoom'), true); return; }
  const cfg = romEmuCfg(state.ctx.rom), was = state.emu ? state.emu.dev : cfg && cfg.dev;
  if (was && was !== dev && !confirm(t('saveEmuOther', { a: E.DEVICES[was].label, b: E.DEVICES[dev].label }))) return;
  state.emu = { guest: g, dev, size: pk.blob.length, big: pk.big }; state.emuRemove = false; state.doom = null;
  renderDoom(); renderEmu();
  $('saveStat').textContent = t('saveEmuInDone', { d: E.DEVICES[dev].label });
};
$('btnParaOut').onclick = () => busy(async () => {
  const ed = state.ctx.editionId, name = edSlug(ed) + '-' + stamp() + '.tamasave';
  download(SV.paraEncode(SV.paraFromImage(state.ctx.rom, ed), ed, null), name);
  $('saveStat').textContent = t('saveNoClock', { n: name });
});
$('btnEmuOut').onclick = () => busy(async () => {
  if (romT7(state.ctx.rom) || saveFor() >= 0) {
    const sv = t7SaveOutSel(); if (!sv) { toast(t('t7NoSave'), true); return; }
    const x = saveFor() >= 0 ? romExtras(state.ctx.rom)[saveFor()] : null;
    const name = 't7emu-' + (x ? x.name.toLowerCase() + '-' : '') + stamp() + '-save.bin'; download(sv, name);
    $('saveStat').textContent = t('saveDone', { n: name }); return;
  }
  const cfg = romEmuCfg(state.ctx.rom); if (!cfg) { toast(t('saveNoEmu'), true); return; }
  const g = await SV.emuGuestAsync((a, n) => state.ctx.rom.subarray(a, a + n), cfg);
  const name = cfg.app + '-' + stamp() + '.sav';
  download(g, name);
  $('saveStat').textContent = t('saveDone', { n: name });
});
/* From the device: Paradise save with its clock, and the TamaEmu save if the device has the emulator */
$('btnSaveRead').onclick = () => devTask(async () => {
  const d = dv.d, ed = d.info.edition, [a, b] = SV.region(ed), cancel = () => dv.cancel;
  const used = await d.usedSectors(a, b, devProg, cancel);
  const pages = new Map(), ff = new Uint8Array(0x1000).fill(0xFF);
  for (let p = a; p < b; p += 0x1000) pages.set(p, ff);
  let n = 0;
  for (const s of used) { if (cancel()) throw new Error('cancelled'); pages.set(s, await d.read(s, 0x1000)); devProg('read', ++n * 0x1000, used.length * 0x1000); }
  const clock = await d.clockRead();
  const names = [edSlug(ed) + '-' + stamp() + '.tamasave'];
  download(SV.paraEncode(pages, ed, clock && (clock.ctrl & 0x80) ? clock : null), names[0]);
  // TamaEmu on the device?
  const sec = await d.read(0xA0000, 0x1000);
  const kindDoom = (() => { for (let i = 0x140; i < 0x400; i += 4) if ((sec[i] | sec[i + 1] << 8 | sec[i + 2] << 16 | sec[i + 3] << 24) >>> 0 === 0x434D4450) return true; return false; })();
  const cfg = kindDoom ? null : SV.emuCfg(sec);
  if ((sec[0x140] | sec[0x141] << 8 | sec[0x142] << 16 | sec[0x143] << 24) >>> 0 === E.T7_MAGIC) {
    // T7Emu: the pet's EEPROM (two slots) as save.bin of the T7Emu app
    const part = new Uint8Array(SV.T7_EE + 0x4000);
    part.set(await d.read(SV.T7_EE, 0x4000), SV.T7_EE);
    const sv = SV.t7SaveOut(part);
    if (sv) { names.push('t7emu-' + stamp() + '-save.bin'); download(sv, names[names.length - 1]); }
  }
  // further T7Emus (1.6.0): where the loaded ROM has them, if the device has the same program there
  if (state.ctx) for (const x of romExtras(state.ctx.rom)) {
    const h = await d.read(x.off, 0x1000);
    if ((h[0x140] | h[0x141] << 8 | h[0x142] << 16 | h[0x143] << 24) >>> 0 !== E.T7_MAGIC || (h[0x150] | h[0x151] << 8 | h[0x152] << 16 | h[0x153] << 24) >>> 0 !== x.saveOff) continue;
    const part = new Uint8Array(x.saveOff + 0x5000);
    part.set(await d.read(x.saveOff + 0x1000, 0x4000), x.saveOff + 0x1000);
    const sv = SV.t7SaveOut(part, x.saveOff);
    if (sv) { names.push('t7emu-' + x.name.toLowerCase() + '-' + stamp() + '-save.bin'); download(sv, names[names.length - 1]); }
  }
  if (cfg) {
    // read less: if the loaded ROM has the same firmware layout, only the sectors that differ
    let base = null;
    const rc = state.ctx && romEmuCfg(state.ctx.rom);
    if (rc && rc.guestOff === cfg.guestOff && rc.guestSize === cfg.guestSize && rc.kind === cfg.kind && rc.map.every((v, i) => v === cfg.map[i])) {
      const end = Math.max(cfg.spareEnd || 0, (cfg.guestOff + cfg.guestSize + 0xFFFF) & ~0xFFFF);
      const diff = await d.changed(state.ctx.rom, devProg, cancel, cfg.guestOff & ~0xFFFF, Math.min(end, 0x1000000));
      base = new Uint8Array(state.ctx.rom);
      for (const s of diff) base.set(await d.read(s, 0x1000), s);
    }
    const read = base ? (x, k) => base.subarray(x, x + k) : (x, k) => d.read(x, k);
    const g = await SV.emuGuestAsync(read, cfg, base ? null : devProg);
    names.push(cfg.app + '-' + stamp() + '.sav');
    download(g, names[1]);
  }
  devRender(t('devSaveGot', { n: names.join(', ') }) + (clock ? '' : ' ' + t('devSaveClockNo')));
});
$('btnSaveWrite').onclick = () => { $('fileSave').value = ''; $('fileSave').click(); };
$('fileSave').onchange = async (e) => {
  const f = e.target.files[0]; if (!f || !dv.d) return;
  let save;
  try { save = SV.paraDecode(new Uint8Array(await f.arrayBuffer())); } catch (err) { toast(em(err), true); return; }
  const ed = dv.d.info.edition;
  const chk = SV.paraCheck(save, ed);
  chk.errors.forEach((x) => { if (x.p) { x.p.s = FL.EDITIONS[x.p.s] || x.p.s; x.p.d = FL.EDITIONS[x.p.d] || x.p.d; } });
  if (chk.errors.length) { devRender(t('devErr', { e: chk.errors.map(saveMsg).join(', ') })); return; }
  const warn = chk.warnings.map(saveMsg).join('\n');
  if (!confirm(t('devSaveSure', { ed: dv.d.info.name }) + (warn ? '\n\n' + warn : ''))) return;
  devTask(async () => {
    const d = dv.d, [a, b] = SV.region(ed);
    // only pages that differ from the device
    const img = new Uint8Array(0x1000000).fill(0xFF);
    for (const [p, data] of chk.pages) img.set(data, p);
    const diff = await d.changed(img, devProg, () => dv.cancel, a & ~0xFFFF, b);
    const map = new Map();
    for (const [p, data] of chk.pages) if (diff.has(p)) map.set(p, data);
    const w = map.size ? await d.writeSectors(map, devProg, () => dv.cancel) : 0;
    const v = SV.paraClock(save);
    const cl = v !== null && await d.clockSet(v);
    devRender(t('devSaveDone', { w, c: cl ? t('devSaveClock') : t('devSaveClockNo') }));
  });
};

window.addEventListener('beforeunload', (e) => { if (state.ctx && state.changed.size && state.saved !== state.changed.size) { e.preventDefault(); e.returnValue = t('leave'); } });

applyLang(); devRender();
window.__paramix = state; // for tests
})();
