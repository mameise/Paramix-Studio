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
    g1t: 'Emulatoren: Farb-Tamas, Tama-Go, Friends, Meets, erste Tamagotchis und Digimon', g1: 'iD L, Plus Color, P\'s, 4U und 4U+ mit TamaEmu, Tama-Go mit Figuren und Friends mit T7Emu, Meets und On mit T18Emu, P1, P2, Angel und weitere sowie Digimon V1 und V2 mit V1Emu. Mehrere zusammen, gestartet über das Menü, jeder mit eigenem Spielstand. Die ROMs bringst du selbst mit.',
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
    planTitle: 'Dein neues ROM', planFig: 'Figuren', planEmu: 'Emulator', planEmus: 'Emulatoren', planDoom: 'DOOM', planMenu: 'Menü', planApps: 'Weitere Emulatoren',
    stNone: 'nicht dabei', stKeep: 'bleibt', stNew: 'neu', stDel: 'wird entfernt', stChanged: '{n} geändert', stSame: 'unverändert',
    segFw: 'Firmware und Figuren', segEmu: 'Emulator', segDoom: 'DOOM', segSlots: 'Spielstände der Felder', segSave: 'Spielstand', segFree: 'frei',
    ruleBoth: 'Emulator oder DOOM: es passt nur eines von beiden ins ROM. Das Menü passt zu beidem.',
    ruleNoRoom: 'In dieser Ausgabe ist kein Platz für Emulator und DOOM (die Figuren-Daten sind grösser). Das Menü geht.',
    emuVsDoom: 'Achtung: DOOM ist gewählt oder eingebaut. Mit einer Firmware hier fällt DOOM weg.',
    doomVsEmu: 'Achtung: ein Emulator ist gewählt oder eingebaut. Mit einer WAD hier fällt der Emulator weg.',
    noRoomApp: 'In dieser Ausgabe ist kein Platz dafür.', a0Menu: 'Diese Ausgabe hat keinen Platz für ein Hauptprogramm bei 0x0A0000: alle Programme kommen ins Menü. Der Stern wählt, was beim Einschalten startet.', a0MenuN: 'Diese Ausgabe hat keinen Platz für ein Hauptprogramm bei 0x0A0000. Tama-Go, Friends, die alten Tamas und das V3 kommen ins Menü (A und C, dafür im Tab Menü Feldwechsel oder Items einschalten). Farb-Tamas und TG18 gehen hier nur im Startgerät.', a0NoMain: '{f}: Farb-Tamas und TG18 gehen bei dieser Ausgabe nur im Startgerät (Tab Menü).',
    doomHas: 'In diesem ROM ist DOOM eingebaut.', doomOff: 'DOOM entfernen', doomGone: 'DOOM wird beim Speichern entfernt.',
    keysTitle: 'Tasten', kFire: 'Schiessen', kFwd: 'Vorwärts', kBack: 'Zurück', kUse: 'Benutzen', kMod: 'Waffe wechseln: halten + Rad',
    kWheel: 'Rad drücken', kRun: 'Laufen mit 2x Vorwärts', kInv: 'Rad andersherum', kFixed: 'Fest: A+C Menü, B+C Karte, Rad 2 s zurück ins Paradise, Haltetaste + Rad drücken Ton.',
    kOld: 'Dieses DOOM ist älter und kennt noch keine eigene Tastenbelegung. Mit der WAD neu einbauen.',
    help: 'Hilfe', helpTitle: 'So geht\'s', h1t: 'Ablauf', h1: '1. ROM öffnen (Datei vom eigenen Paradise) oder mit "Gerät" direkt auslesen.\n2. In den Tabs wählen, was hinein soll: Figuren, Emulator, DOOM, Menü.\n3. Speichern oder mit "Gerät" aufs Paradise schreiben. Mit "Spielstände behalten" bleibt der Spielstand auf dem Gerät.', h2t: 'Tasten am Paradise (TamaEmu, T7Emu, T18Emu)', hk1: 'A und C', hv1: 'auf dem Heimbildschirm: Emulator oder Menü starten', hk2: 'Rad lange', hv2: 'Stand speichern und zurück ins Paradise', hk3: 'Rad kurz', hv3: 'T7Emu: Einstellungen und Figur', hk4: 'Rad in den ersten 3 s', hv4: 'gespeicherten Stand verwerfen (mit A bestätigen), der nächste Start beginnt frisch ab dem eigenen Spielstand des Tamas', hk5: 'B beim Einschalten', hv5: 'mit Autostart: Paradise statt Emulator', hk6: 'Reset-Knopf', hv6: 'mit Autostart: der Emulator startet wieder',  h3t: 'Uhr', h3: 'Mit "Uhr beim Start vom Paradise übernehmen" (Tab Emulator) stellt der Emulator bei jedem Start seine Uhr auf die Zeit des Paradise. Was dazwischen war, wird nicht nachgespielt. Die Uhr also am Paradise stellen.', h4t: 'Wieder reingehen', h4: 'Der Emulator macht genau dort weiter, wo du mit dem Rad rausgegangen bist. Der eigene Spielstand des Tamas bleibt auch dann, wenn der gespeicherte Stand verworfen wird.', h5t: 'Menü und Felder', h5: 'A und C auf dem Heimbildschirm. Rad wählt, B bestätigt, C geht zurück. Beim Feldwechsel wird der Spielstand des alten Felds gepackt und der des neuen zurückgeholt.', h6t: 'Spielstände sichern', h6: 'Tab Spielstand oder Gerät, "Spielstand sichern". Vor einem Wechsel des Emulators sichern, der alte Spielstand geht dabei verloren.', device: 'Gerät', devTitle: 'Gerät über UART', devHint: 'UART-Dongle mit 3,3 V an die drei Kontakte: links RX vom Dongle, Mitte GND, rechts TX vom Dongle. Am Paradise den Download-Bildschirm öffnen (Planet, A, Connect, Download, Download), dann Verbinden. Braucht Chrome oder Edge am Computer.',
    devConnect: 'Verbinden', devDump: 'ROM auslesen', devWrite: 'Neues ROM aufs Gerät schreiben', devOnly: 'Nur Änderungen schreiben', devKeep: 'Spielstände behalten', devStop: 'Abbrechen',
    devNoSerial: 'Dieser Browser kann nicht mit dem Dongle reden. Bitte Chrome oder Edge am Computer verwenden.', devNone: 'Nicht verbunden.',
    devOn: 'Verbunden: {ed}, {mb} MB{l}', devLoaded: ', Flash-App geladen', devReady: 'Bereit.', devNeedRom: 'Zum Schreiben zuerst ein ROM öffnen (oder auslesen).',
    visTitle: 'Besuch simulieren (Paradise)', visStart: 'Starten', visStop: 'Beenden',
    visHint: 'Das Studio spielt ein zweites Paradise. Dongle an die Kontakte, das Paradise läuft normal.\n1. Edition wählen, Starten, Dongle wählen.\n2. Am Paradise: Planet-Menü, VERBINDUNG, PLAYDATE, dann B.\nGeheime Charaktere: im Jugendalter zwei andere Farben besuchen.\nOben "Verbinden" ist nur fürs ROM, für den Besuch nicht drücken.',
    visWait: 'Bereit als {ed}. Am Paradise PLAYDATE wählen und B drücken.', visRun: 'Verbunden, Daten werden getauscht…', visPlay: 'Die beiden spielen…',
    visDone: 'Besuch fertig ({ed}). Für den nächsten wieder B am Paradise.', visErr: 'Abgebrochen: {e}. Am Paradise nochmals B.', visBadKey: 'Das Paradise versteht die Daten nicht.',
    phload: 'Flash-App laden', phread: 'Auslesen', phcompare: 'Vergleichen', phwrite: 'Schreiben', phcheck: 'Prüfen', phprepare: 'Vorbereiten',
    devDumped: 'ROM ausgelesen und als {n} gespeichert. Es ist jetzt im Studio geöffnet.',
    devSure: 'Das neue ROM wird jetzt aufs Gerät geschrieben. Gerät während des Schreibens eingeschaltet lassen. Auf eigene Gefahr schreiben?',
    devSureFull: 'Ohne "Spielstände behalten" wird auch der Spielstand mit dem aus dem ROM überschrieben. Trotzdem schreiben?',
    devEmuLost: 'Hauptprogramm auf dem Gerät: {o}. Im neuen ROM: {n}. Der Spielstand von {o} als Hauptprogramm fängt deshalb neu an. Zum Behalten vorher unter "Spielstand" sichern. Trotzdem schreiben?', devEmuNone: 'keinen Emulator', devEmuNew: '{o} mit einer neu gewählten Firmware',
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
    fl_noteNoFit: 'Feld {f} passt nicht ins neue Fach und wird nicht behalten', fl_noteMoved: 'Feld {f} ist ins neue Fach umgezogen', fl_noteSaveMoved: 'Spielstand von {n} mitgenommen',
    fl_noteDropped: 'das neue ROM hat kein Feld-Menü, die gespeicherten Felder auf dem Gerät fallen weg', fl_noteEdKept: 'das Gerät ist {d}, das neue ROM ist {r}: gleiche Firmware, Spielstand und Feld vom Gerät bleiben ({d})', fl_noteEmu: 'TamaEmu-Firmware neu gewählt, der Emulator startet frisch. Für ein Update das ROM nur öffnen und speichern',
    launchOn: 'Nur Startgerät für Programme', launchHint: 'Vom Paradise bleibt nur die Einrichtung (Sprache, Uhr, Weiter oder Neu). Danach kommt immer das Menü mit deinen Emulatoren und DOOM. Alle Bilder des Spiels fallen weg, das gibt etwa 5 MB mehr Platz.', launchUse: 'Nach dem Einschalten und dem Stellen der Uhr kommt das Menü. Rad wählt, B startet. Zurück aus einem Programm wie gewohnt, dann kommt wieder das Menü.', launchOld: 'Dieses ROM ist ein Startgerät. Für ein normales Paradise das Original-ROM laden.', launchNeed: 'Ein Startgerät braucht mindestens ein Programm (Emulator oder DOOM).', launchSix: 'Startgerät geht nicht mit eingebauten sechs Feldern. Bitte das Original-ROM laden.', planLaunch: 'Startgerät',
    tabFeld: 'Menü', feldOn: 'Mehrere Spielstände (mit Feldwahl)', bagOn: 'Items und Tamapoints', tamaOn: 'Tama ändern (Stufe, Pflege, Figur)', saveSlot: 'Spielstand {n}',
    sixOn: 'Alle 6 Felder', sixPick: 'Land-, Water- oder Sky-ROM wählen', sixHint: 'Holt Land, Water und Sky zu Tropical und Glacier dazu. Die Feldbilder kommen aus deinem eigenen Land-, Water- oder Sky-ROM.',
    sixNeed: 'Für alle 6 Felder fehlt noch ein Land-, Water- oder Sky-ROM.', sixOk: 'Feldbilder aus {n} übernommen.', sixBad: '{n} ist kein Land-, Water- oder Sky-ROM.', sixIn: 'Alle 6 Felder sind eingebaut.', sixOff: 'Beim Speichern kommen Land, Water und Sky wieder raus. Mehrere Spielstände gehen weiterhin.', sixOffJ: 'Beim Speichern kommt Sky wieder raus. Mehrere Spielstände gehen weiterhin.', sixOffField: 'Das Gerät ist gerade auf {f}. Erst im Menü auf {s} wechseln.', sixOffSlots: 'Die im Menü gespeicherten Spielstände werden dabei gelöscht. Weiter?', sixFail: 'Felder nicht eingebaut: {e}', sixNoFw: 'Die Firmware dieses ROMs ist verändert, weitere Felder gehen hier nicht.', planSix: 'Mehr Felder', sixOnJ: 'Sky als 4. Feld', sixHintJ: 'Jade Forest hat die Sky-Bilder schon dabei.', sixInJ: 'Sky ist als 4. Feld eingebaut.', sixOld: 'Alle 6 Felder sind eingebaut, aber in einer älteren Version. Wähl dein Land-, Water- oder Sky-ROM, dann wird beim Speichern alles nachgerüstet.', sixOldJ: 'Sky ist eingebaut und wird beim Speichern auf den neuen Stand gebracht.', sixPlanet: 'Planet passend zum Feld', sixMenu: 'Spielmenü Felder: alle Felder ab Stufe 4', sixPlanetNeed: 'Zum Ändern dieser Einstellung braucht es nochmals dein Land-, Water- oder Sky-ROM.', sixApp: 'DOOM und Emulator passen nicht dazu',
    feldHint: 'Kleines Menü im ROM: zwischen Spielständen wechseln (neuer Spielstand auf einem Feld nach Wahl), Items in die Tasche legen, Tamapoints setzen, Tama ändern.',
    feldUse: 'Öffnen: auf dem Heimbildschirm A und C zusammen halten. Rad wählt, B bestätigt, C zurück.',
    feldNo: 'Feldwahl geht mit Land, Water, Sky (untereinander) und Tropical, Glacier (untereinander).', feldMissing: 'In dieser Ausgabe fehlt das Menü.',
    feldCur: 'Aktives Feld: {f}.', feldList: 'Wählbar: {s}.', feldStored: 'Gespeichert: {s}.', feldFail: 'Menü nicht eingebaut: {e}', feldUnknown: 'Ausgabe nicht erkannt.',
    tabGb: 'Game Boy', gbNoMain: 'Game Boy nur im Menü', gbPick: 'Spiele wählen (.gb)', gbHint: 'Die Spiele kommen ins Paramix-Menü. Am Gerät: A+B+C zusammen öffnet die Einstellungen (dort auch Select und Ton), Rad 2 s halten führt zurück.', gbNone2: 'Noch kein Spiel gewählt.', gbKeysTitle: 'Tasten', gbKeysDef: 'Standard', gbKeysMiss: 'Nicht belegt: {n}', gbNone: '–', gbRight: 'Rechts', gbLeft: 'Links', gbUp: 'Hoch', gbDown: 'Runter', gbInA: 'Taste A', gbInB: 'Taste B', gbInC: 'Taste C', gbInWp: 'Rad drücken', gbInWu: 'Rad hoch', gbInWd: 'Rad runter', gbInAC: 'A + C', gbInBC: 'B + C', gbInAB: 'A + B', gbBad: '{f}: kein Game-Boy-Spiel', gbColor: '{f}: nur für Game Boy Color, das geht nicht', gbNeedRom: 'Zuerst ein ROM öffnen (oder vom Gerät lesen).',
    tabFigs: 'Figuren', tabAll: 'Alle Sprites', tabDim: 'Karte', tabEmu: 'Emulator', tabDoom: 'DOOM',
    doomPick: 'DOOM.WAD wählen', doomEp: 'Episode', doomMusic: 'Musik',
    doomHint: 'Baut DOOM ins ROM. Die WAD bringst du selbst mit, sie bleibt auf deinem Computer.',
    doomUse: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad 2 s drücken.',
    doomNone: 'Keine WAD gewählt.', doomOk: 'WAD erkannt ({eps}). DOOM wird beim Speichern eingebaut.',
    doomBad: 'WAD nicht erkannt: {e}', doomFail: 'DOOM nicht eingebaut: {e}', doomMissing: 'In dieser Ausgabe fehlt das DOOM-Programm.',
    doomBoth: 'Emulator und DOOM zusammen passen nicht in ein ROM. Bitte nur eines wählen.',
    doomBig: 'Keine Episode dieser WAD passt ins ROM.', doomNoMusic: 'Diese Episode passt nur ohne Musik.',
    emuPick: 'Firmware oder ROM wählen', emuOff: 'Emulator entfernen',
    emuHint: 'Farb-Tamas (TamaEmu), Tama-Go und Friends (T7Emu), Meets und On (T18Emu), erste Tamagotchis und Digimon (V1Emu), Connection V3 (V3Emu). Das erste ist das Hauptprogramm, jedes weitere kommt ins Menü. Der volle Stern zeigt das Hauptprogramm, ein Klick auf einen leeren Stern macht ein anderes dazu. Die Dateien bringst du selbst mit.', t7Ok: 'V7-ROM erkannt (T7Emu). Es wird beim Speichern eingebaut.', t7OkFr: 'Tamagotchi Friends erkannt (T7Emu, Test). Es wird beim Speichern eingebaut. Figuren gibt es dafür keine.', t7Has: 'In diesem ROM ist T7Emu eingebaut.', t7HasFr: 'In diesem ROM ist T7Emu mit Tamagotchi Friends eingebaut.', t7Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Rad kurz: Einstellungen und Figur. Hängt T7Emu: mit A und C starten, loslassen und in den ersten 3 Sekunden das Rad drücken und mit A bestätigen.', t7FigPick: 'Figuren hinzufügen', t7FigNone: 'Noch keine Figuren.', t7FigBad: '{f}: keine Figur.', t7FigFull: 'Kein Platz mehr für {f}.', t7FigMax: 'Kein Platz für weitere Figuren.', t7FigDup: '{f} ist schon dabei.', appPick: 'Weiteren Emulator dazu', appHint: 'Noch ein Tama-Go- oder Friends-ROM (T7Emu). Start über das Menü (A und C), dafür muss im Tab Menü etwas eingeschaltet sein.', appNone: 'Keine weiteren Emulatoren.', appBad: '{f}: kein Tama-Go- oder Friends-ROM.', appBadL: '{f}: weder Tama-Go-, Friends-ROM noch Farb-Tama- oder TG18-Firmware.', appT18One: 'T18Emu geht nur einmal pro ROM.', appHintL: 'Im Startgerät: Tama-Go- oder Friends-ROM, Farb-Tama-Firmware (TamaEmu) oder TG18-Firmware (T18Emu, nur einmal).', ruleLaunch: 'Startgerät: Emulatoren und DOOM passen zusammen, solange Platz frei ist.', appMax: 'Höchstens 12 weitere Emulatoren.', appNeedMenu: 'Weitere Emulatoren starten über das Menü: im Tab Menü Feldwechsel oder Items einschalten.', appFail: 'Weitere Emulatoren: {e}', appSix: 'Sechs Felder gehen nicht zusammen mit weiteren Emulatoren.', t7SaveBtn: 'T7Emu-Spielstand (.bin)', saveFor: 'Programm', saveMain: 'Haupt-Emulator', v1SaveBtn: 'V1Emu-Spielstand (.bin)', saveT18No: 'T18Emu: Spielstand sichern geht hier noch nicht.', t7SaveXDone: 'Spielstand für {n} übernommen. Jetzt das ROM speichern oder aufs Gerät schreiben.', t7NoSave: 'Das Tama hat noch keinen Spielstand.', t7SaveInDone: 'T7Emu-Spielstand übernommen. Jetzt das ROM speichern oder aufs Gerät schreiben.', t7SaveNeed: 'Dafür muss T7Emu im ROM sein.', saveEmuAny: 'Emulator-Spielstand (.sav / .bin)',
    emuUse: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Hängt TamaEmu: mit A und C starten, loslassen und in den ersten 3 Sekunden das Rad drücken und mit A bestätigen. Dann beginnt er neu ab dem eigenen Spielstand des Tamas.',
    emuNone: 'Kein Emulator gewählt.', petT: 'Spielstand ändern', petOpen: 'Paradise-Spielstand öffnen', petRom: 'Spielstand im ROM', petClose: 'Schliessen', petRomDone: 'Im ROM geändert. Jetzt das ROM speichern oder aufs Gerät schreiben (ohne "Spielstände behalten", sonst bleibt der Spielstand des Geräts).', petLevel: 'Stufe', petChar: 'Tama', petCare: 'Satt, glücklich, gesund', petSave: 'Geänderten Spielstand speichern', petKeep: 'unverändert', petKid: 'Kind', petYoung: 'Teen', petAdult: 'Erwachsen', petNow: 'Im Spielstand: Stufe {l}, Tama {c}, Hunger {h}/6, Laune {j}/20{s}.', petSick: ', krank', petOther: 'Der Spielstand ist für eine andere Ausgabe als das geladene ROM.', petNoSet: 'Im Spielstand fehlt der Einstellungsblock.', petDone: '{n} gespeichert. Mit "Spielstand aufs Gerät" übertragen.', petNothing: 'Nichts geändert.', tabSave: 'Spielstand', tabNames: 'Namen', cardNoChars: 'keine Figuren auf dieser Karte', noRoomKB: 'Zu wenig Platz: es fehlen etwa {k} KB. Ein Programm entfernen.', noRoomAdd: '{n} hat keinen Platz mehr: es fehlen etwa {k} KB.', eyeMvT: 'Augen verschieben', mouthMvT: 'Mund verschieben', poseNr: 'Pose {i} von {n}', faceBig: 'Gross', faceSmall: 'Klein', poseOnly: 'Nur diese Pose', poseOnlyHint: 'Die Pfeile verschieben Augen oder Mund nur in der gezeigten Pose. Für eigene Figuren, bei denen einzelne Posen nicht passen.', eyeMvHint: 'Gilt auf dem Feld für Tamas, die nach dem Schreiben schlüpfen oder sich entwickeln. Die kleine Figur rückt halb so weit.', planEyes: 'Augen und Mund', namesHint: 'Namen ändern: Grossbuchstaben, Ziffern, Leerzeichen und . & \' ! ? /. Gilt für alle Sprachen mit lateinischer Schrift auf dem Gerät.', namesFields: 'Felder', namesChars: 'Tamas', namesItems: 'Items', namesFind: 'Suchen', namesUndo: 'Alle zurücksetzen', planNames: 'Namen', planNamesN: '{n} geändert', saveParaOut: 'Paradise-Spielstand (.tamasave)', saveEmuOut: 'TamaEmu-Spielstand (.sav)', saveInfo: 'Exportieren holt die Spielstände aus diesem ROM. Ein Emulator-Spielstand (TamaEmu .sav oder T7Emu .bin) kommt ins ROM, danach das ROM speichern oder aufs Gerät schreiben. Ein Paradise-Spielstand geht direkt aufs Gerät, dafür oben das Gerät verbinden.', saveOutT: 'Exportieren', saveInT: 'Einspielen', saveSecProg: 'Programme', saveSecPara: 'Paradise', saveParaOutB: 'Exportieren (.tamasave)', saveParaInB: 'Aufs Gerät schreiben (.tamasave)', saveProgInfo: 'Exportieren holt den Spielstand des gewählten Programms aus diesem ROM. Einspielen bringt ihn ins ROM, danach das ROM speichern oder aufs Gerät schreiben.', saveParaInfo: 'Exportieren holt den Paradise-Spielstand aus diesem ROM. Aufs Gerät schreiben geht direkt, dafür oben das Gerät verbinden. Platz: welcher Spielstand.', saveSlotL: 'Platz', slotLive: '{n} (aktiv)', slotEmpty: '{n} (leer)', slotNone: 'Auf diesem Platz ist kein Spielstand.', slotField: 'Der Spielstand ist für {a}, der Platz für {b}.', slotDevOther: 'Das Menü auf dem Gerät passt nicht zum geladenen ROM. Zuerst das ROM vom Gerät laden.', slotSure: 'Der Spielstand kommt auf dem Gerät nach {n} und ersetzt den dortigen. Weiter?', slotDone: 'Fertig: {n} auf dem Gerät ersetzt.', saveNoProg: 'In diesem ROM ist noch kein Programm. Ein TamaEmu- oder T7Emu-Spielstand bringt seinen Emulator mit.', saveBtnOut: 'Exportieren ({f})', saveBtnIn: 'Einspielen ({f})', saveEmuInDone: 'TamaEmu-Spielstand übernommen ({d}). Jetzt das ROM speichern oder aufs Gerät schreiben.', saveEmuOther: 'Im ROM ist TamaEmu für {a}, der Spielstand ist für {b}. Trotzdem übernehmen?', saveNoRoom: 'Für TamaEmu ist in diesem ROM kein Platz.', saveEmuBad: 'Das ist kein Emulator-Spielstand (TamaEmu .sav oder T7Emu .bin).', saveNoEmu: 'In diesem ROM ist kein Emulator.', saveDone: '{n} gespeichert.', saveNoClock: '{n} gespeichert. Die Uhr ist nicht dabei, in der App eventuell neu stellen.', devSaveRead: 'Spielstand sichern', devSaveWrite: 'Spielstand aufs Gerät', devSaveHint: 'Sichern holt den Paradise-Spielstand mit Uhr und den Spielstand von TamaEmu oder T7Emu. Aufs Gerät geht nur der Paradise-Spielstand.', devSaveSure: 'Der Spielstand wird auf das Gerät ({ed}) geschrieben und ersetzt den jetzigen. Weiter?', devSaveDone: 'Fertig: {w} Seiten geschrieben. {c} Das Gerät kann neu gestartet werden.', devSaveClock: 'Uhr übernommen.', devSaveClockNo: 'Die Uhr bitte am Gerät stellen.', devSaveGot: 'Gesichert: {n}.', fl_saveOtherEd: 'der Spielstand ist von einer anderen Ausgabe ({s}), das Gerät ist {d}', fl_saveFamily: 'der Spielstand passt nicht zu dieser Ausgabe', fl_saveNoSettings: 'im Spielstand fehlt der Einstellungsblock, das ist kein vollständiger Paradise-Spielstand', fl_saveOutside: '{n} Seiten liegen ausserhalb des Spielstands und werden nicht geschrieben', fl_saveIncomplete: 'der Spielstand enthält nicht alle Seiten, fehlende bleiben wie auf dem Gerät', emuUndo: 'Auswahl zurücknehmen', progMain: 'Start mit A und C', progMainL: 'Erster Eintrag im Menü', progMenu: 'Start im Menü', progGone: 'wird entfernt', progUndo: 'Zurücknehmen', progDel: 'Entfernen', progNone: 'Noch kein Emulator. Firmware oder ROM wählen, jedes weitere kommt ins Menü.', appBadMain: '{f}: Farb-Tama- und TG18-Firmware gehen als weiteres Programm nur im Startgerät. Sonst erst den Emulator oben mit × entfernen.', emuInList: 'Im ROM als weitere Programme: {n}. Nicht nochmals wählen.', emuNfc: '4U: NFC-Chip nachbilden', emuNfcHint: 'Das Menü reagiert auf einen Druck. Ohne Haken wie bisher.', emuClock: 'Uhr beim Start vom Paradise übernehmen', emuClockHint: 'Bei jedem Start gilt die Zeit des Paradise. Die Zeit dazwischen wird nicht nachgespielt.', emuAuto: 'Beim Einschalten starten', emuAutoHint: 'Rad lange drücken: zurück ins Paradise, es bleibt dort. Wieder in den Emulator: A und C oder Reset-Knopf.', autoHintL: 'Startet beim Einschalten statt des Menüs. B beim Einschalten halten: Menü.', emuHas: 'In diesem ROM ist ein Emulator eingebaut.', emuBig: 'Diese Firmware ist gross: der Feldwechsel im Menü hat daneben keinen Platz.', emuUp: 'Beim Speichern kommt die neue Emulator-Version hinein. Mit "Spielstände behalten" bleibt der Spielstand.',
    emuOk: 'Firmware erkannt ({size}). Sie wird beim Speichern eingebaut.', emuBad: 'Firmware nicht erkannt: {e}', t18Ok: 'TG18-Firmware erkannt (T18Emu, Meets/On). Sie wird beim Speichern eingebaut.', t18Has: 'In diesem ROM ist T18Emu eingebaut.', t18Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Zurück: Rad lange drücken (speichert). Hängt T18Emu: mit A und C starten, loslassen, in den ersten 3 Sekunden das Rad drücken und mit A bestätigen. Dann startet die Firmware neu ab ihrem eigenen Spielstand. Experimentell: das Tama läuft noch langsamer als echt.',
    emuGone: 'Emulator wird beim Speichern entfernt.', emuMissing: 'In dieser Ausgabe fehlt das Emulator-Programm.',
    emuFail: 'Emulator nicht eingebaut: {e}', savedEmu: 'Gespeichert: {name}',
    t18Pics: 'Bilder für Erwachsen', t18PicsPick: 'Bilder wählen', t18PicsDel: 'Bilder entfernen', t18PicsOk: '{n} Bilder übernommen.', t18PicsUnknown: '{f}: unbekannter Name', t18PicsHint: 'Am Gerät zeigt Erwachsen dann das Bild der Wahl. PNG-Dateien, Name = Figur (z.B. Mametchi.png, Kuchipatchi.png) oder ihre Nummer (16.png). Der Ordner erwachsen aus dem Android-T18Emu geht auch: alle Dateien samt erwachsen.json wählen. Jede Grösse, wird auf 64x64 verkleinert.', t18Items: 'Items', t18ItemsPick: 'MyMeets-Dateien wählen', t18ItemsNoSave: 'Items gehen, sobald das Spiel einmal gelaufen ist (ROM vom Gerät lesen).', t18ItemsHint: 'Items, Raum und Tamapoints kommen in den Spielstand des Spiels. Beim Schreiben ersetzt das den Spielstand von T18Emu auf dem Gerät, auch mit «Spielstände behalten». Das Tama bleibt, der laufende Stand fängt neu an.', t18ItemsNone: 'Keine Items in den Dateien gefunden.', t18ItemsOther: '{n} ist für {m}, nicht für die Meets/On. Ob es dort geht, ist nicht sicher.', t18ItemsFull: 'kein Platz mehr', mmTitle: 'MixingMeets (Besuch, Heirat)', uartBusy: 'Paramix Studio ist noch mit dem Dongle verbunden. So lange geht die Brücke nicht.', uartFree: 'UART freigeben', uartFreed: 'Dongle freigegeben. Das Paradise neu starten, damit das Spiel wieder läuft.', uartRun: 'Erst warten, bis das Gerät fertig ist.', pairTitle: 'Zwei Paradise verbinden (Besuch, Spiel)', pairHint: 'Beide Paradise oben zusammenstecken, an beiden T18Emu starten. Dann an beiden CONNECTION, TAMAGOTCHI und weiter wie bei zwei Tamas.', pairHintDigi: 'Beide Paradise oben zusammenstecken, an beiden V1Emu mit Digimon starten. Dann den Kampf wählen und starten wie bei zwei echten Digimon.', pairHintPs: 'Beide Paradise oben zusammenstecken, an beiden TamaEmu (P\'s) starten. Dann an beiden Verbindung, たまごっちP\'s, あそぶ. Am einen あそびにいく, am anderen ともだちをよぶ, ふたりで, ein Spiel, dann B.', pairHintIdl: 'Beide Paradise oben zusammenstecken, an beiden TamaEmu (iD L) starten. Dann an beiden die Verbindung im Spiel wählen wie bei zwei echten iD L.', pairHintV3: 'Beide Paradise oben zusammenstecken, an beiden V3Emu starten. Dann an beiden die Verbindung im Spiel wählen wie bei zwei echten V3.', pairHintT7: 'Beide Paradise oben zusammenstecken, an beiden T7Emu (Tama-Go) starten. Dann an beiden Verbindung, TAMA-GO, VISIT oder GAME. Am Gerät, das besuchen geht, B drücken.', mmLink: 'Paramix-Brücke', mmDrag: 'In die Lesezeichenleiste ziehen, dann auf mixing-meets.com anklicken.', mmHint: '1. Den Knopf Paramix-Brücke einmal in die Lesezeichenleiste ziehen (Chrome oder Edge).\n2. Paramix Studio vom Dongle trennen (UART freigeben oder das Studio schliessen). Dongle an die Kontakte wie beim Flashen. Am Tama: CONNECTION, APP, VISIT (zum Zurückholen: A drücken, GO BACK, YES).\n3. Auf mixing-meets.com Connect oder Return öffnen, das Lesezeichen anklicken, dann Select Tamagotchi und den Dongle wählen.\nNach jedem Seitenwechsel das Lesezeichen nochmals anklicken.', t7Gp: 'Gotchi Points', t7GpHint: 'Kommt wie ein eingespielter Spielstand ins ROM: das Tama bleibt, der laufende Stand fängt neu an (am Gerät DOWNLOAD wählen).', t18KeepAsk: 'T18Emu: Items oder Tamapoints geändert. Sein Spielstand auf dem Gerät wird durch den aus dem ROM ersetzt (Stand beim Auslesen), auch mit «Spielstände behalten». Weiter?', t18ItemNo: 'Nr. {n}', t18ItemOk: '{n} liegt jetzt bei {k}.', t18Points: 'Tamapoints', t18A_food: 'Essen', t18A_item: 'Gegenstände', t18A_acc: 'Accessoires', t18A_room: 'Raum', t18RoomBack: 'eigener Raum weg', t18RoomBuiltin: 'eingebauter Raum', dlcPick: 'Download-Inhalte hinzufügen', dlcInfo: 'Belegte Plätze zeigen', progMore: 'Einstellungen und Inhalt', progMakeMain: 'Zum Hauptprogramm machen', progIsMain: 'Hauptprogramm', v1Ok: 'Erstes Tamagotchi oder Digimon erkannt (V1Emu).', v3Ok: 'Tamagotchi Connection V3 erkannt (V3Emu).', v3Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Rad kurz: Einstellungen. Zurück: Rad lange drücken.', optVol: 'Lautstärke', optVolN: 'normal', optVolQ: 'leise', optVolL: 'laut', optOff: 'aus', optDim: 'Helligkeit', optScr: 'Bild aus nach', optDev: 'Auch am Gerät: Rad kurz drücken. Dort Geändertes gilt, bis du es hier wieder änderst.', optScrToy: 'wie am Tama', v1Use: 'Starten: auf dem Heimbildschirm A und C zusammen halten. Rad kurz: Einstellungen. Rad drehen: schütteln. Zurück: Rad lange drücken.', progNoMainDoom: 'DOOM ist das Hauptprogramm. Für ein anderes zuerst DOOM entfernen (×).', doomAutoHint: 'Rad 2 s drücken: zurück ins Paradise, es bleibt dort.', progReplaced: '{n} ist jetzt das Hauptprogramm statt {o}.', appTwice: '{n} ist schon dabei.', progNoMainV1: 'V1Emu startet nur aus dem Menü', progNoMain: 'Geht nur im Startgerät', progMainNow: '{n} ist jetzt das Hauptprogramm. Die Spielstände der getauschten Programme fangen neu an.', dlcLoading: 'Wird gelesen…', dlcNewArea: 'Dieses Item braucht einen neuen Speicherblock. Der Spielstand dieses Emulators fängt nach dem Schreiben neu an. Items in schon benutzten Bereichen behalten ihn.', dlcHint: 'Essen, Kleider, Accessoires, Spiele usw. als .jpg-Download-Dateien. Mehrere Dateien auf einmal gehen, bei P\'s einen VDP-Satz immer ganz.', dlcFreeN: '{n} frei', dlcDel: 'entfernen', dlcK0: 'Mahlzeiten', dlcK1: 'Snacks', dlcK2: 'Kleider', dlcK3: 'Accessoires', dlcK4: 'Gegenstände', dlcK5: 'Alltagsdinge', dlcK6: 'Hintergründe', dlcK7: 'Spiele', dlcK8: 'Ausflugsziele', dlcK10: 'VDP-Rezepte', dlcK15: 'Dekorationen', dlcK16: 'Gutscheine', dlcK17: 'Bingo', dlcK18: 'Gashapon', dlcK19: 'Figuren', dlcOk: '{n} liegt jetzt in {k}, Platz {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} entfernt.',
    figField: 'Feld:', figAll: 'alle', zipField: 'Feld als ZIP', fieldZipOk: '{f}: {n} Figuren und {s} Bilder als ZIP.', fieldIn: 'Feld aus ZIP übernehmen', fieldInBad: 'Kein Feld-ZIP (field.json fehlt).', fieldInOk: '{src} in {dst} übernommen: {n} Teile.', fieldInEyes: '{k} Figuren bleiben original, ihre Augenbilder sind hier zu gross.',
    zipFigs: 'Alle Figuren als ZIP', zipAll: 'Alle Sprites als ZIP', upload: 'Sheets hochladen (PNG oder ZIP)',
    numbers: 'Nummern-Test-ROM',
    figHint: 'Eine Figur besteht aus Körper, Augen und Mund, je gross und klein. Klick auf eine Figur für ihr Sheet.',
    allHint: 'Hintergründe, Menüs, Gegenstände, Schriften.',
    moreColors: 'Mehr Farben erlauben (bis 256)',
    dimLoad: 'Karte laden', dimHint: 'Abbild einer Figurenkarte (4 MB) oder einer TamaSma-Karte (1 oder 2 MB). Kartenfiguren ersetzen am besten Erwachsene. Baby, Kind und Teen teilen sich die Augen und bleiben original.',
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
    upOk: 'Übernommen: {n}', upSkip: 'Übersprungen: {list}', upNone: 'Keine passenden Dateien gefunden. Erwartet: figur_0237.png oder sprite_0014.png.', sprSix: 'Feldbild der zusätzlichen Felder, nicht änderbar.',
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
    g1t: 'Emulators: colour tamas, Tama-Go, Friends, Meets, first Tamagotchis and Digimon', g1: 'iD L, Plus Color, P\'s, 4U and 4U+ with TamaEmu, Tama-Go with figures and Friends with T7Emu, Meets and On with T18Emu, P1, P2, Angel and more plus Digimon V1 and V2 with V1Emu. Several together, started from the menu, each with its own save. Bring your own ROMs.',
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
    planTitle: 'Your new ROM', planFig: 'Characters', planEmu: 'Emulator', planEmus: 'Emulators', planDoom: 'DOOM', planMenu: 'Menu', planApps: 'More emulators',
    stNone: 'not included', stKeep: 'stays', stNew: 'new', stDel: 'will be removed', stChanged: '{n} changed', stSame: 'unchanged',
    segFw: 'Firmware and characters', segEmu: 'Emulator', segDoom: 'DOOM', segSlots: 'Field saves', segSave: 'Save', segFree: 'free',
    ruleBoth: 'Emulator or DOOM: only one of the two fits into the ROM. The menu works with both.',
    ruleNoRoom: 'This edition has no room for emulator and DOOM (its character data is larger). The menu works.',
    emuVsDoom: 'Note: DOOM is chosen or built in. Choosing a firmware here drops DOOM.',
    doomVsEmu: 'Note: an emulator is chosen or built in. Choosing a WAD here drops the emulator.',
    noRoomApp: 'This edition has no room for it.', a0Menu: 'This edition has no room for a main program at 0x0A0000: every program goes to the menu. The star chooses what starts when switched on.', a0MenuN: 'This edition has no room for a main program at 0x0A0000. Tama-Go, Friends, the old Tamas and the V3 go to the menu (A and C; switch on field switch or items in the Menu tab). Colour Tamas and TG18 work here in a start device only.', a0NoMain: '{f}: colour Tamas and TG18 work in this edition in a start device only (Menu tab).',
    doomHas: 'This ROM contains DOOM.', doomOff: 'Remove DOOM', doomGone: 'DOOM is removed on save.',
    keysTitle: 'Buttons', kFire: 'Fire', kFwd: 'Forward', kBack: 'Back', kUse: 'Use', kMod: 'Change weapon: hold + wheel',
    kWheel: 'Wheel press', kRun: 'Run with 2x forward', kInv: 'Wheel reversed', kFixed: 'Fixed: A+C menu, B+C map, wheel 2 s back to Paradise, hold button + wheel press sound.',
    kOld: 'This DOOM is older and has no own button layout yet. Build it in again with the WAD.',
    help: 'Help', helpTitle: 'How it works', h1t: 'Steps', h1: '1. Open a ROM (file of your own Paradise) or read it straight from the device with "Device".\n2. Choose in the tabs what goes in: figures, emulator, DOOM, menu.\n3. Save, or write it to the Paradise with "Device". With "Keep saves" the save on the device stays.', h2t: 'Buttons on the Paradise (TamaEmu, T7Emu, T18Emu)', hk1: 'A and C', hv1: 'on the home screen: start the emulator or the menu', hk2: 'Wheel long', hv2: 'save the state and go back to the Paradise', hk3: 'Wheel short', hv3: 'T7Emu: settings and figure', hk4: 'Wheel in the first 3 s', hv4: 'drop the saved state (confirm with A), the next start begins fresh from the Tama\'s own save', hk5: 'B when switching on', hv5: 'with autostart: Paradise instead of the emulator', hk6: 'Reset button', hv6: 'with autostart: the emulator starts again',  h3t: 'Clock', h3: 'With "Take the clock from the Paradise at the start" (tab Emulator) the emulator sets its clock to the Paradise\'s time at every start. The time in between is not played through. So set the clock on the Paradise.', h4t: 'Coming back', h4: 'The emulator continues exactly where you left with the wheel. The Tama\'s own save stays even when the saved state is dropped.', h5t: 'Menu and fields', h5: 'A and C on the home screen. The wheel selects, B confirms, C goes back. Switching the field packs the save of the old field and brings back the one of the new field.', h6t: 'Backing up saves', h6: 'Tab Save, or Device, "Back up save". Back up before changing the emulator, the old save is lost then.', device: 'Device', devTitle: 'Device over UART', devHint: 'UART dongle with 3.3 V on the three contacts: left RX from the dongle, middle GND, right TX from the dongle. Open the download screen on the Paradise (Planet, A, Connect, Download, Download), then Connect. Needs Chrome or Edge on a computer.',
    devConnect: 'Connect', devDump: 'Read ROM', devWrite: 'Write new ROM to the device', devOnly: 'Only write changes', devKeep: 'Keep saves', devStop: 'Cancel',
    devNoSerial: 'This browser cannot talk to the dongle. Please use Chrome or Edge on a computer.', devNone: 'Not connected.',
    devOn: 'Connected: {ed}, {mb} MB{l}', devLoaded: ', flash app loaded', devReady: 'Ready.', devNeedRom: 'To write, open a ROM first (or read one).',
    visTitle: 'Simulate a visit (Paradise)', visStart: 'Start', visStop: 'Stop',
    visHint: 'The Studio plays a second Paradise. Dongle on the contacts, the Paradise runs normally.\n1. Choose the edition, Start, choose the dongle.\n2. On the Paradise: planet menu, CONNECTION, PLAYDATE, then B.\nSecret characters: visit two other colours at the young stage.\n"Connect" above is only for the ROM, do not press it for a visit.',
    visWait: 'Ready as {ed}. On the Paradise choose PLAYDATE and press B.', visRun: 'Connected, exchanging data…', visPlay: 'The two are playing…',
    visDone: 'Visit done ({ed}). For the next one press B on the Paradise again.', visErr: 'Stopped: {e}. Press B on the Paradise again.', visBadKey: 'The Paradise does not understand the data.',
    phload: 'Loading flash app', phread: 'Reading', phcompare: 'Comparing', phwrite: 'Writing', phcheck: 'Checking', phprepare: 'Preparing',
    devDumped: 'ROM read and saved as {n}. It is now open in the Studio.',
    devSure: 'The new ROM is written to the device now. Keep the device switched on while writing. Write at your own risk?',
    devSureFull: 'Without "Keep saves" the save on the device is replaced by the one in the ROM. Write anyway?',
    devEmuLost: 'Main program on the device: {o}. In the new ROM: {n}. So the save of {o} as the main program starts anew. To keep it, back it up first under "Save". Write anyway?', devEmuNone: 'no emulator', devEmuNew: '{o} with a newly chosen firmware',
    devDone: 'Done and checked: {w} sectors written{k}. Restart the device (reset or battery).', devKept: ', {k} save sectors kept',
    devSame: 'Nothing to write, the device already has this ROM.', devErr: 'Error: {e}', devCancelled: 'Cancelled.',
    launchOn: 'Start device for programs only', launchHint: 'Only the setup of the Paradise stays (language, clock, continue or new). After it the menu with your emulators and DOOM always comes up. All pictures of the game go, which gives about 5 MB more room.', launchUse: 'After switching on and setting the clock the menu comes up. Wheel selects, B starts. Leave a program as usual, then the menu comes up again.', launchOld: 'This ROM is a start device. Load the original ROM for a normal Paradise.', launchNeed: 'A start device needs at least one program (emulator or DOOM).', launchSix: 'A start device does not go with six fields built in. Please load the original ROM.', planLaunch: 'Start device',
    tabFeld: 'Menu', feldOn: 'Several saves (with field choice)', bagOn: 'Items and Tamapoints', tamaOn: 'Change Tama (level, care, character)', saveSlot: 'Save {n}',
    sixOn: 'All 6 fields', sixPick: 'Choose Land, Water or Sky ROM', sixHint: 'Adds Land, Water and Sky to Tropical and Glacier. The field pictures come from your own Land, Water or Sky ROM.',
    sixNeed: 'All 6 fields still need a Land, Water or Sky ROM.', sixOk: 'Field pictures taken from {n}.', sixBad: '{n} is not a Land, Water or Sky ROM.', sixIn: 'All 6 fields are built in.', sixOff: 'Saving takes Land, Water and Sky out again. Several saves keep working.', sixOffJ: 'Saving takes Sky out again. Several saves keep working.', sixOffField: 'The device is on {f} now. First switch to {s} in the menu.', sixOffSlots: 'The saves stored in the menu are deleted. Continue?', sixFail: 'Fields not built in: {e}', sixNoFw: 'The firmware of this ROM is changed, more fields do not work here.', planSix: 'More fields', sixOnJ: 'Sky as 4th field', sixHintJ: 'Jade Forest already has the Sky pictures.', sixInJ: 'Sky is built in as the 4th field.', sixOld: 'All 6 fields are built in, but an older version. Choose your Land, Water or Sky ROM and everything is updated when you save.', sixOldJ: 'Sky is built in and is updated when you save.', sixPlanet: 'Planet to match the field', sixMenu: 'Game menu Fields: all fields from level 4', sixPlanetNeed: 'Changing this setting needs your Land, Water or Sky ROM again.', sixApp: 'DOOM and the emulator do not fit with it',
    feldHint: 'Small menu in the ROM: switch between saves (a new save on a field of your choice), put items in the bag, set Tamapoints, change the Tama.',
    feldUse: 'Open: hold A and C together on the home screen. Wheel selects, B confirms, C goes back.',
    feldNo: 'Field switch works with Land, Water, Sky (among each other) and Tropical, Glacier (among each other).', feldMissing: 'This build does not contain the menu.',
    feldCur: 'Active field: {f}.', feldList: 'Selectable: {s}.', feldStored: 'Saved: {s}.', feldFail: 'Menu not built in: {e}', feldUnknown: 'Edition not recognised.',
    tabGb: 'Game Boy', gbNoMain: 'Game Boy only in the menu', gbPick: 'Choose games (.gb)', gbHint: 'The games go into the Paramix menu. On the device: A+B+C together opens the settings (also Select and sound), hold the wheel for 2 s to go back.', gbNone2: 'No game chosen yet.', gbKeysTitle: 'Keys', gbKeysDef: 'Default', gbKeysMiss: 'Not assigned: {n}', gbNone: '–', gbRight: 'Right', gbLeft: 'Left', gbUp: 'Up', gbDown: 'Down', gbInA: 'Key A', gbInB: 'Key B', gbInC: 'Key C', gbInWp: 'Wheel press', gbInWu: 'Wheel up', gbInWd: 'Wheel down', gbInAC: 'A + C', gbInBC: 'B + C', gbInAB: 'A + B', gbBad: '{f}: not a Game Boy game', gbColor: '{f}: Game Boy Color only, that does not work', gbNeedRom: 'Open a ROM first (or read it from the device).',
    tabFigs: 'Characters', tabAll: 'All sprites', tabDim: 'Card', tabEmu: 'Emulator', tabDoom: 'DOOM',
    doomPick: 'Choose DOOM.WAD', doomEp: 'Episode', doomMusic: 'Music',
    doomHint: 'Builds DOOM into the ROM. Bring your own WAD, it stays on your computer.',
    doomUse: 'Start: hold A and C together on the home screen. Back: hold the wheel for 2 s.',
    doomNone: 'No WAD chosen.', doomOk: 'WAD recognised ({eps}). DOOM is built in on save.',
    doomBad: 'WAD not recognised: {e}', doomFail: 'DOOM not built in: {e}', doomMissing: 'This build does not contain the DOOM program.',
    doomBoth: 'Emulator and DOOM do not fit into one ROM together. Please choose only one.',
    doomBig: 'No episode of this WAD fits into the ROM.', doomNoMusic: 'This episode only fits without music.',
    emuPick: 'Choose firmware or ROM', emuOff: 'Remove emulator',
    emuHint: 'Colour tamas (TamaEmu), Tama-Go and Friends (T7Emu), Meets and On (T18Emu), first Tamagotchis and Digimon (V1Emu), Connection V3 (V3Emu). The first is the main program, every further one goes to the menu. The filled star marks the main program, a click on an empty star makes another one the main program. Bring your own files.', t7Ok: 'V7 ROM recognised (T7Emu). It is built in on save.', t7OkFr: 'Tamagotchi Friends recognised (T7Emu, test). It is built in on save. There are no figures for it.', t7Has: 'This ROM contains T7Emu.', t7HasFr: 'This ROM contains T7Emu with Tamagotchi Friends.', t7Use: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). Wheel short: settings and figure. If T7Emu hangs: start with A and C, release them and press the wheel within the first 3 seconds and confirm with A.', t7FigPick: 'Add figures', t7FigNone: 'No figures yet.', t7FigBad: '{f}: not a figure.', t7FigFull: 'No room left for {f}.', t7FigMax: 'No room for more figures.', t7FigDup: '{f} is already in.', appPick: 'Add another emulator', appHint: 'One more Tama-Go or Friends ROM (T7Emu). Started from the menu (A and C), so something in the Menu tab must be switched on.', appNone: 'No other emulators.', appBad: '{f}: not a Tama-Go or Friends ROM.', appBadL: '{f}: neither a Tama-Go or Friends ROM nor a colour Tama or TG18 firmware.', appT18One: 'T18Emu goes only once per ROM.', appHintL: 'In a start device: Tama-Go or Friends ROM, colour Tama firmware (TamaEmu) or TG18 firmware (T18Emu, once only).', ruleLaunch: 'Start device: emulators and DOOM fit together as long as there is room.', appMax: 'At most 12 more emulators.', appNeedMenu: 'More emulators start from the menu: switch on the field switch or items in the Menu tab.', appFail: 'More emulators: {e}', appSix: 'Six fields do not go together with more emulators.', t7SaveBtn: 'T7Emu save (.bin)', saveFor: 'Program', saveMain: 'Main emulator', v1SaveBtn: 'V1Emu save (.bin)', saveT18No: 'T18Emu: backing up its save is not possible here yet.', t7SaveXDone: 'Save for {n} taken over. Now save the ROM or write it to the device.', t7NoSave: 'The Tama has no save yet.', t7SaveInDone: 'T7Emu save taken over. Now save the ROM or write it to the device.', t7SaveNeed: 'T7Emu has to be in the ROM for that.', saveEmuAny: 'Emulator save (.sav / .bin)',
    emuUse: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). If TamaEmu hangs: start with A and C, release them and press the wheel within the first 3 seconds and confirm with A. It then starts over from the save the Tama keeps itself.',
    emuNone: 'No emulator chosen.', petT: 'Change save', petOpen: 'Open Paradise save', petRom: 'Save in the ROM', petClose: 'Close', petRomDone: 'Changed in the ROM. Now save the ROM or write it to the device (without "Keep saves", otherwise the device keeps its save).', petLevel: 'Level', petChar: 'Tama', petCare: 'Fed, happy, healthy', petSave: 'Save changed save', petKeep: 'unchanged', petKid: 'Kid', petYoung: 'Teen', petAdult: 'Adult', petNow: 'In the save: level {l}, Tama {c}, hunger {h}/6, happiness {j}/20{s}.', petSick: ', sick', petOther: 'The save is for another edition than the loaded ROM.', petNoSet: 'The settings block is missing in the save.', petDone: '{n} saved. Send it with "Save to device".', petNothing: 'Nothing changed.', tabSave: 'Save', tabNames: 'Names', cardNoChars: 'no characters on this card', noRoomKB: 'Not enough room: about {k} KB missing. Remove a program.', noRoomAdd: 'No room left for {n}: about {k} KB missing.', eyeMvT: 'Move the eyes', mouthMvT: 'Move the mouth', poseNr: 'Pose {i} of {n}', faceBig: 'Big', faceSmall: 'Small', poseOnly: 'This pose only', poseOnlyHint: 'The arrows move eyes or mouth only in the pose shown. For your own figures where single poses do not fit.', eyeMvHint: 'Applies on the field to Tamas that hatch or grow after writing. The small figure moves half as far.', planEyes: 'Eyes and mouth', namesHint: 'Change names: capitals, digits, space and . & \' ! ? /. Applies to every language with Latin letters on the device.', namesFields: 'Fields', namesChars: 'Tamas', namesItems: 'Items', namesFind: 'Search', namesUndo: 'Reset all', planNames: 'Names', planNamesN: '{n} changed', saveParaOut: 'Paradise save (.tamasave)', saveEmuOut: 'TamaEmu save (.sav)', saveInfo: 'Export takes the saves out of this ROM. An emulator save (TamaEmu .sav or T7Emu .bin) goes into the ROM, then save the ROM or write it to the device. A Paradise save goes straight to the device, connect it above for that.', saveOutT: 'Export', saveInT: 'Import', saveSecProg: 'Programs', saveSecPara: 'Paradise', saveParaOutB: 'Export (.tamasave)', saveParaInB: 'Write to device (.tamasave)', saveProgInfo: 'Export takes the save of the chosen program out of this ROM. Import puts it into the ROM, then save the ROM or write it to the device.', saveParaInfo: 'Export takes the Paradise save out of this ROM. Write to device goes straight to the device, connect it above for that. Slot: which save.', saveSlotL: 'Slot', slotLive: '{n} (active)', slotEmpty: '{n} (empty)', slotNone: 'There is no save in this slot.', slotField: 'The save is for {a}, the slot for {b}.', slotDevOther: 'The menu on the device does not match the loaded ROM. Load the ROM from the device first.', slotSure: 'The save goes into {n} on the device and replaces the one there. Continue?', slotDone: 'Done: {n} replaced on the device.', saveNoProg: 'No program in this ROM yet. A TamaEmu or T7Emu save brings its emulator along.', saveBtnOut: 'Export ({f})', saveBtnIn: 'Import ({f})', saveEmuInDone: 'TamaEmu save taken over ({d}). Now save the ROM or write it to the device.', saveEmuOther: 'The ROM has TamaEmu for {a}, the save is for {b}. Take it anyway?', saveNoRoom: 'This ROM has no room for TamaEmu.', saveEmuBad: 'This is not an emulator save (TamaEmu .sav or T7Emu .bin).', saveNoEmu: 'This ROM has no emulator.', saveDone: '{n} saved.', saveNoClock: '{n} saved. The clock is not included, set it in the app if needed.', devSaveRead: 'Back up save', devSaveWrite: 'Save to device', devSaveHint: 'Back up takes the Paradise save with the clock and the save of TamaEmu or T7Emu. Only the Paradise save goes to the device.', devSaveSure: 'The save is written to the device ({ed}) and replaces the current one. Continue?', devSaveDone: 'Done: {w} pages written. {c} The device can be restarted.', devSaveClock: 'Clock taken over.', devSaveClockNo: 'Please set the clock on the device.', devSaveGot: 'Backed up: {n}.', fl_saveOtherEd: 'the save is from another edition ({s}), the device is {d}', fl_saveFamily: 'the save does not fit this edition', fl_saveNoSettings: 'the settings block is missing, this is not a complete Paradise save', fl_saveOutside: '{n} pages lie outside the save and are not written', fl_saveIncomplete: 'the save does not hold every page, missing ones stay as on the device', emuUndo: 'Undo choice', progMain: 'Starts with A and C', progMainL: 'First entry of the menu', progMenu: 'Starts from the menu', progGone: 'will be removed', progUndo: 'Undo', progDel: 'Remove', progNone: 'No emulator yet. Choose firmware or ROM, every further one goes to the menu.', appBadMain: '{f}: colour Tama and TG18 firmware go as a further program only in a start device. Otherwise remove the emulator above with × first.', emuInList: 'In the ROM as further programs: {n}. No need to choose them again.', emuNfc: '4U: emulate the NFC chip', emuNfcHint: 'The menu reacts to one press. Unticked it works as before.', emuClock: 'Take the clock from the Paradise at the start', emuClockHint: 'At every start the Paradise\'s time applies. The time in between is not played through.', emuAuto: 'Start when switched on', emuAutoHint: 'Hold the wheel: back to the Paradise, it stays there. Back into the emulator: A and C, or the reset button.', autoHintL: 'Starts at switching on instead of the menu. Hold B while switching on: menu.', emuHas: 'This ROM contains an emulator.', emuBig: 'This firmware is big: the field switch of the menu has no room next to it.', emuUp: 'Saving puts in the new emulator version. With "Keep saves" the save stays.',
    emuOk: 'Firmware recognised ({size}). It is built in on save.', emuBad: 'Firmware not recognised: {e}', t18Ok: 'TG18 firmware recognised (T18Emu, Meets/On). It is built in on save.', t18Has: 'This ROM contains T18Emu.', t18Use: 'Start: hold A and C together on the home screen. Back: hold the wheel (saves). If T18Emu hangs: start with A and C, release them, press the wheel within the first 3 seconds and confirm with A. The firmware then starts over from its own save. Experimental: the Tama still runs slower than the real one.',
    emuGone: 'The emulator is removed on save.', emuMissing: 'This build does not contain the emulator program.',
    emuFail: 'Emulator not built in: {e}', savedEmu: 'Saved: {name}',
    t18Pics: 'Pictures for grow up', t18PicsPick: 'Choose pictures', t18PicsDel: 'Remove pictures', t18PicsOk: '{n} pictures taken.', t18PicsUnknown: '{f}: unknown name', t18PicsHint: 'On the device grow up then shows the picture of the choice. PNG files, name = character (e.g. Mametchi.png, Kuchipatchi.png) or its number (16.png). The folder erwachsen of the Android T18Emu works too: choose all files with erwachsen.json. Any size, scaled to 64x64.', t18Items: 'Items', t18ItemsPick: 'Choose MyMeets files', t18ItemsNoSave: 'Items work once the game has run (read the ROM from the device).', t18ItemsHint: 'Items, room and Tamapoints go into the game\'s save. When written it replaces T18Emu\'s save on the device, even with "Keep saves". The Tama stays, the running state starts anew.', t18ItemsNone: 'No items found in the files.', t18ItemsOther: '{n} is for {m}, not for the Meets/On. It may not work there.', t18ItemsFull: 'no room left', mmTitle: 'MixingMeets (visit, marriage)', uartBusy: 'Paramix Studio is still connected to the dongle. The bridge does not work meanwhile.', uartFree: 'Release UART', uartFreed: 'Dongle released. Restart the Paradise so the game runs again.', uartRun: 'Wait until the device is done.', pairTitle: 'Connect two Paradise (visit, game)', pairHint: 'Plug both Paradise together at the top and start T18Emu on both. Then on both CONNECTION, TAMAGOTCHI and on as with two Tamas.', pairHintDigi: 'Plug both Paradise together at the top and start V1Emu with Digimon on both. Then choose and start the battle as with two real Digimon.', pairHintPs: 'Plug both Paradise together at the top and start TamaEmu (P\'s) on both. Then on both connection, たまごっちP\'s, あそぶ. On one あそびにいく, on the other ともだちをよぶ, ふたりで, a game, then B.', pairHintIdl: 'Plug both Paradise together at the top and start TamaEmu (iD L) on both. Then choose the connection in the game on both as with two real iD L.', pairHintV3: 'Plug both Paradise together at the top and start V3Emu on both. Then choose the connection in the game on both as with two real V3.', pairHintT7: 'Plug both Paradise together at the top and start T7Emu (Tama-Go) on both. Then on both connection, TAMA-GO, VISIT or GAME. Press B on the device that goes visiting.', mmLink: 'Paramix bridge', mmDrag: 'Drag it to the bookmarks bar, then click it on mixing-meets.com.', mmHint: '1. Drag the Paramix bridge button to the bookmarks bar once (Chrome or Edge).\n2. Disconnect Paramix Studio from the dongle (Release UART or close the Studio). Dongle on the contacts as for flashing. On the Tama: CONNECTION, APP, VISIT (to bring it back: press A, GO BACK, YES).\n3. On mixing-meets.com open Connect or Return, click the bookmark, then Select Tamagotchi and choose the dongle.\nClick the bookmark again after every page change.', t7Gp: 'Gotchi Points', t7GpHint: 'Goes into the ROM like an imported save: the Tama stays, the running state starts anew (choose DOWNLOAD on the device).', t18KeepAsk: 'T18Emu: items or Tamapoints changed. Its save on the device is replaced by the one in the ROM (as read), even with "Keep saves". Go on?', t18ItemNo: 'No. {n}', t18ItemOk: '{n} is now in {k}.', t18Points: 'Tamapoints', t18A_food: 'Food', t18A_item: 'Items', t18A_acc: 'Accessories', t18A_room: 'Room', t18RoomBack: 'remove own room', t18RoomBuiltin: 'built-in room', dlcPick: 'Add download content', dlcInfo: 'Show used slots', progMore: 'Settings and content', progMakeMain: 'Make it the main program', progIsMain: 'Main program', v1Ok: 'First Tamagotchi or Digimon recognised (V1Emu).', v3Ok: 'Tamagotchi Connection V3 recognised (V3Emu).', v3Use: 'Start: hold A and C together on the home screen. Wheel short: settings. Back: hold the wheel.', optVol: 'Volume', optVolN: 'normal', optVolQ: 'quiet', optVolL: 'loud', optOff: 'off', optDim: 'Brightness', optScr: 'Screen off after', optDev: 'Also on the device: press the wheel briefly. A change made there applies until you change it here again.', optScrToy: 'as on the Tama', v1Use: 'Start: hold A and C together on the home screen. Wheel short: settings. Turn the wheel: shake. Back: hold the wheel.', progNoMainDoom: 'DOOM is the main program. For another one remove DOOM first (×).', doomAutoHint: 'Hold the wheel for 2 s: back to the Paradise, it stays there.', progReplaced: '{n} is now the main program instead of {o}.', appTwice: '{n} is already in.', progNoMainV1: 'V1Emu starts from the menu only', progNoMain: 'Only in a start device', progMainNow: '{n} is now the main program. The saves of the swapped programs start anew.', dlcLoading: 'Reading…', dlcNewArea: 'This item needs a new memory block. The save of this emulator starts anew after writing. Items in areas already in use keep it.', dlcHint: 'Food, clothes, accessories, games etc. as .jpg download files. Several files at once work, on the P\'s always a whole VDP set.', dlcFreeN: '{n} free', dlcDel: 'remove', dlcK0: 'Meals', dlcK1: 'Snacks', dlcK2: 'Clothing', dlcK3: 'Accessories', dlcK4: 'Items', dlcK5: 'Daily items', dlcK6: 'Wallpapers', dlcK7: 'Games', dlcK8: 'Destinations', dlcK10: 'VDP recipes', dlcK15: 'Decorations', dlcK16: 'Coupons', dlcK17: 'Bingo', dlcK18: 'Gashapon', dlcK19: 'Characters', dlcOk: '{n} now sits in {k}, slot {s}.', dlcBad: '{f}: {e}', dlcGone: '{n} removed.',
    figField: 'Field:', figAll: 'all', zipField: 'Field as ZIP', fieldZipOk: '{f}: {n} characters and {s} pictures as ZIP.', fieldIn: 'Load field from ZIP', fieldInBad: 'Not a field ZIP (field.json missing).', fieldInOk: '{src} applied to {dst}: {n} parts.', fieldInEyes: '{k} characters stay original, their eye images are too big here.',
    zipFigs: 'All characters as ZIP', zipAll: 'All sprites as ZIP', upload: 'Upload sheets (PNG or ZIP)',
    numbers: 'Number test ROM',
    figHint: 'A character is made of body, eyes and mouth, each large and small. Click a character for its sheet.',
    allHint: 'Backgrounds, menus, items, fonts.',
    moreColors: 'Allow more colours (up to 256)',
    dimLoad: 'Load card', dimHint: 'Dump of a character card (4 MB) or a TamaSma card (1 or 2 MB). Card characters best replace adults. Baby, child and teen share their eyes and stay original.',
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
    upOk: 'Applied: {n}', upSkip: 'Skipped: {list}', upNone: 'No matching files. Expected: figur_0237.png or sprite_0014.png.', sprSix: 'Field picture of the extra fields, cannot be changed.',
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
  [/^no room for (.+) \((\d+) KB\)$/, 'kein Platz für $1 ($2 KB)'],
  [/^no room for (.+)$/, 'kein Platz für $1'],
  [/^only one T18Emu$/, 'T18Emu geht nur einmal'],
  [/^T18Emu needs the area at 0x0F0000, the main program uses it: choose T18Emu in the Emulator tab$/, 'T18Emu braucht den Bereich ab 0x0F0000, das Hauptprogramm belegt ihn: T18Emu im Tab Emulator wählen'],
  [/^(.+): only in a start device$/, '$1: nur im Startgerät'],
  [/^launcher template does not fit$/, 'Starter-Vorlage passt nicht'],
  [/^too many scenes$/, 'zu viele Szenen'],
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
    renderInfo(); renderDim(); renderFieldPick(); renderNames(); renderEmu(); renderDoom(); renderFeld(); renderSave();
    document.querySelectorAll('#gridFigs .tile').forEach((b) => { const g = +b.dataset.idx; b.querySelector('.cap span').textContent = [state.charNames.get(g), groupRole(g)].filter(Boolean).join(' '); });
    if (state.open) openDrawer(state.open.kind, state.open.idx);
  }
  try { if (!dv.run) devRender(); } catch (e) {}   // dv does not exist yet on the first call
}
document.querySelectorAll('[data-lang]').forEach(b => b.onclick = () => { lang = b.dataset.lang; try { localStorage.setItem('paramix-lang', lang); } catch (e) {} applyLang(); });

// ------------------------------------------------------------------ state
const state = { ctx: null, name: '', changed: new Map(), dim: null, open: null, zoom: 2, decoded: new Map(), eyeOffsets: new Map(), eyeRects: new Map(), eyeLayer: false, fields: [], charNames: new Map() };
const entry = (i) => state.changed.get(i) || (state.launch && state.launchMap && state.launchMap.get(i)) || state.ctx.entries[i];
const menuWanted = () => !!(state.feld || state.bag || state.tama || state.launch);
/* launcher ROM (1.6.8): the small images, computed once per ROM */
function launchMapGet() { if (!state.launchMap) state.launchMap = C.launcherChanges(state.ctx); return state.launchMap; }
function decode(i) {
  const e = entry(i);
  let d = state.decoded.get(i);
  if (!d || d.src !== e) { d = { src: e, dec: C.spriteDecode(e) }; state.decoded.set(i, d); }
  return d.dec;
}

// ------------------------------------------------------------ helpers
function toast(msg, err) { if (toast.mute) { if (err) toast.mute.err = msg; return; } const el = $('toast'); el.textContent = msg; el.className = 'toast on' + (err ? ' err' : ''); clearTimeout(toast.t); toast.t = setTimeout(() => el.className = 'toast', err ? 9000 : 5000); }
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
const baseName = () => state.name.replace(/\.[^.]+$/, '').replace(/(_paramix|_emu|_t7|_t18|_doom_E\d|_felder|_fields|_4fields|_6fields|_items|_start)+$/, '');

// ------------------------------------------------------------- loading
async function loadRom(file) {
  const buf = new Uint8Array(await file.arrayBuffer());
  let ctx;
  try { ctx = C.openRom(buf); } catch (e) { toast(t('notRom', { e: em(e) }), true); return; }
  Object.assign(state, { ctx, name: file.name, changed: new Map(), decoded: new Map(), open: null, eyeOffsets: new Map() });
  $('start').hidden = true; $('work').classList.add('on'); $('btnSave').disabled = false;
  $('romname').textContent = file.name;
  state.emu = null; state.t7save = null; state.t7saveX = null; state.saveIn = null; state.emuRemove = false; state.doom = null; state.doomKeys = null; state.six = null; state.sixName = ''; state.sixWant = false; state.sixOff = false; state.petDirty = false; state.texts = new Map(); state.eyeShift = new Map(); state.mouthShift = new Map(); state.poseAdj = new Map(); state.poseCache = null; face.g = -1; state.dlcT = 'main'; state.figT = 'main'; state.dlcOpen = false; state.mainGuest = null; state.t18Main = null; state.t18Dirty = false; state.t18Pics = E.t18PicsRead(buf); state.t18PicsDirty = false; state.itemT = 'main'; state.progOpen = new Set(); state.mainPick = null; state.mainDirty = false;
  { const si = S6.sixInfo(buf); state.sixPlanet = si && si.version >= 5 ? si.planet : true; state.sixMenu = si && si.version >= 6 ? si.fieldMenu : true; }
  { const l = stubList(buf); const g = E.autoGet(buf, l); state.emuAuto = g === null ? E.autoGetOld(buf, l) : g; }
  { const c = E.emuClockGet(buf); state.emuClock = c === null ? true : c; }
  { const c = E.emuNfcGet(buf); state.emuNfc = c === null ? true : c; }
  state.emuOpt = E.emuOptGet(buf) || { vol: 0, dim: 0, off: 0 };
  { const fe = F.embedded(), f = fe && F.feldFind(buf, fe.stub);
    state.feld = !!(f && (f.flags & F.F_FIELDS)); state.bag = !!(f && (f.flags & F.F_BAG)); state.tama = !!(f && (f.flags & F.F_TAMA)); state.saves = true;
    state.feldInit = state.feld; state.bagInit = state.bag; state.tamaInit = state.tama; state.savesInit = true;
    state.launch = !!(f && f.version >= 2 && (f.flags & F.F_LAUNCH)); state.launchInit = state.launch; state.launchMap = null;
    state.extras = f ? E.appsRead(buf, F.feldApps(buf, fe.stub).apps).map((x) => x.kind === 'tama' ? { kind: 'tama', guest: x.guest, dev: x.dev, clock: x.clock, nfc: x.nfc, opt: x.opt } : x.kind === 't18' ? { kind: 't18', fw: x.fw, clock: x.clock, opt: x.opt, keepOff: x.off } : x.kind === 'v1' ? { kind: 'v1', rom: x.rom, clock: x.clock } : x.kind === 'gb' ? { kind: 'gb', rom: x.rom, keys: x.keys, inv: x.inv, save: x.save } : { rom: x.rom, figs: x.figs || [], clock: x.clock }) : []; state.extrasDirty = false;
    state.extrasUp = f ? extrasOld(buf, F.feldApps(buf, fe.stub).apps) : false;   // 1.6.8: programs of an older Studio are renewed on save
    // 1.6.9 test13: start device with DOOM, the main program out of the menu's list and its autostart
    if (f && state.launch) { const fa = F.feldApps(buf, fe.stub);
      if (fa.v9) { state.emuAuto = !!fa.auto; const k = fa.mainSrc ? E.appsRead(buf, fa.apps).findIndex((x) => x.off === fa.mainSrc - 0x10000000 + 0x11000) : -1; state.mainPick = k >= 0 ? state.extras[k] || null : null; } }
    // A menu from an older Studio version is rebuilt on save (e.g. 0.9.5 had no item pictures)
    state.feldStale = false;
    if (f) {
      try {
        const embOld = { stub: fe.stub, oldStub: E.embedded() && E.embedded().stub };
        const fa = F.feldApps(buf, fe.stub), low = fa.apps.length ? Math.min(...fa.apps.map((a) => a.src - 0x10000000 + 0x11000)) : 0;
        const dl = f.version >= 7 ? (buf[f.off + 0x334] | buf[f.off + 0x335] << 8 | buf[f.off + 0x336] << 16 | buf[f.off + 0x337] << 24) >>> 0 : 0, dlo = dl ? dl - 0x10000000 + 0x11000 : 0;
        const re = F.feldApply(F.feldStrip(buf, embOld, E), { fields: state.feld, bag: state.bag, tama: state.tama, saves: state.saves, launch: state.launch, apps: fa.apps, name0: fa.name0, appsLow: low, dl, mainSrc: fa.mainSrc, auto: fa.auto, fieldNames: fieldTexts(buf).map((x) => x.name) }, F.feldSlots(buf, fe.stub), { feld: fe.feld, stub: fe.stub }, E);
        if (dlo) re.set(buf.subarray(dlo, dlo + 0x10000), dlo);   // the flash app stays where it is
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
  renderInfo(); renderGrids(); renderNames(); renderDim(); renderEmu(); renderDoom(); renderFeld(); renderSave(); if (!dv.run) devRender();
  toast(t('loaded', { ed: ctx.edition, s: ctx.entries.length, f: ctx.groups.length }));
}
// ------------------------------------------------------------------ names (1.6.8)
/* Field names: text numbers per field (the field's own one, and 64..66 where LAND/WATER/SKY stand a second time) */
/* 1.6.8: the extra fields (Land, Water, Sky) are part of the ROM being made: when six fields are switched on in the
 * Menu tab, their names can be changed before they are built in */
const sixActive = () => !!state.ctx && !state.launch && ((!!S6.sixInfo(state.ctx.rom) && !state.sixOff) || !!state.six || !!state.sixWant);
function fieldTexts(rom, six) {
  let fn = F.fieldNames(rom).map((s) => s.toUpperCase()); const lws = ['LAND', 'WATER', 'SKY'], out = [];
  if (six !== undefined) {
    const id = F.feldId(rom);
    if (!six) fn = fn.slice(0, 3);
    else if (fn.length <= 3) fn = fn.concat(id === 4 ? ['SKY'] : id >= 5 ? lws : []);
  }
  fn.forEach((nm, i) => {
    const k = lws.indexOf(nm), own = i < 3 ? 14 + i : k >= 0 ? 64 + k : -1;
    if (own < 0) return;
    const idx = [own]; if (i < 3 && k >= 0 && C.textGet(rom, 64 + k) === C.textGet(rom, own)) idx.push(64 + k);
    out.push({ idx, name: C.textGet(rom, own) });
  });
  return out;
}
function nameRow(box, label, cur, idx, max) {
  const l = document.createElement('label'), sp = document.createElement('span'), inp = document.createElement('input');
  sp.textContent = label; sp.title = label; inp.maxLength = max;
  const ed = state.texts.get(idx[0]); inp.value = ed !== undefined ? ed : cur; inp.classList.toggle('chg', ed !== undefined);
  inp.onchange = () => {
    const v = C.textClean(inp.value);
    for (const i of idx) { if (!v || v === cur) state.texts.delete(i); else state.texts.set(i, v); }
    inp.value = v && v !== cur ? v : cur; inp.classList.toggle('chg', !!v && v !== cur); renderPlan();
  };
  l.append(sp, inp); box.appendChild(l);
}
function renderNames() {
  if (!state.ctx) return;
  if (!state.texts) state.texts = new Map();
  const rom = state.ctx.rom, fb = $('namesFields'), cb = $('namesChars'), ib = $('namesItems');
  fb.innerHTML = ''; cb.innerHTML = ''; ib.innerHTML = '';
  $('namesFind').placeholder = t('namesFind');
  for (const f of fieldTexts(rom, sixActive())) nameRow(fb, f.name, f.name, f.idx, 11);
  const seen = new Set();
  try {
    for (const c of C.charTable(state.ctx.files[0]).filter((c) => c.id > 1000 && c.id < 9000)) {
      if (seen.has(c.name)) continue; const nm = C.textGet(rom, c.name); if (!nm) continue; seen.add(c.name);
      nameRow(cb, nm, nm, [c.name], 12);
    }
  } catch (e) { /* no character table */ }
  const q = ($('namesFind').value || '').toUpperCase();
  for (const it of F.itemCatalog(rom)) {
    if (seen.has(it.text)) continue; const nm = C.textGet(rom, it.text); if (!nm) continue; seen.add(it.text);
    if (q && !nm.includes(q) && !(state.texts.get(it.text) || '').includes(q)) continue;
    nameRow(ib, nm, nm, [it.text], 22);
  }
}
$('namesFind').oninput = () => renderNames();
$('namesUndo').onclick = () => { state.texts = new Map(); renderNames(); renderPlan(); };
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
  ctx.entries.forEach((e, i) => { const h = C.spriteHeader(e); const tl = tile('spr', i, h.sw * Math.max(1, h.iw) + '×' + h.sh * Math.max(1, h.ih)); if (sixAdded(i)) { tl.classList.add('locked'); tl.title = t('sprSix'); } ga.appendChild(tl); });
}
function refreshTile(kind, idx) {
  document.querySelectorAll('.tile[data-idx="' + idx + '"][data-kind="' + kind + '"]').forEach(paintTile);
  if (kind === 'spr') { const g = state.ctx.groups.find(g => idx >= g && idx < g + 6); if (g !== undefined) refreshTile('fig', g); }
  else for (let k = 0; k < 6; k++) document.querySelectorAll('.tile[data-idx="' + (idx + k) + '"][data-kind="spr"]').forEach(paintTile);
  renderInfo();
}
document.querySelectorAll('.tab').forEach(b => b.onclick = () => {
  document.querySelectorAll('.tab').forEach(x => x.setAttribute('aria-selected', String(x === b)));
  ['figs', 'all', 'names', 'dim', 'emu', 'doom', 'gb', 'feld', 'save'].forEach(k => $('pane-' + k).hidden = k !== b.dataset.tab);
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
/* 1.6.8: pictures the six fields added (they share their data and are made again when the fields are updated) */
function sixAdded(i) { const si = S6.sixInfo(state.ctx.rom); return !!si && si.kind === 'gen3' && i >= si.sprites; }
function applySheet(kind, idx, img) {
  if (kind === 'spr' && sixAdded(idx)) throw new Error(t('sprSix'));
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
    if (sixAdded(idx)) li(t('sprSix'));
    if (h.numPal > 1) li(t('mKeep'));
  }
  const ch = kind === 'fig' ? [0, 1, 2, 3, 4, 5].some(k => state.changed.has(idx + k)) : state.changed.has(idx);
  $('dReset').disabled = !ch;
  $('dEyes').hidden = kind !== 'fig'; if (kind === 'fig') { if (face.g !== idx) $('dPoseOnly').checked = false; eyePreview(idx); }
  $('dUp').disabled = kind === 'spr' && sixAdded(idx);
  $('drawer').classList.add('open'); $('drawer').setAttribute('aria-hidden', 'false'); $('scrim').classList.add('on');
}
/* 1.6.8: move a character's eyes and mouth (composite entries of the field). The preview shows every pose of the big
 * or the small figure. Arrows move eyes or mouth in all poses (small figure half as far) or, with "this pose only",
 * the layers of the pose shown (state.poseAdj: figure -> word positions -> extra offset). */
const shTxt = (sh) => (sh[0] || sh[1] ? (sh[0] > 0 ? '+' : '') + sh[0] + ' / ' + (sh[1] > 0 ? '+' : '') + sh[1] : '');
const face = { g: -1, pose: 0, small: false };
function posesOf(g, small) {
  if (!state.poseCache) state.poseCache = new Map();
  const base = small ? g + 3 : g, k = state.ctx.files[0].length + ':' + base;
  if (!state.poseCache.has(k)) state.poseCache.set(k, C.facePoses(state.ctx.files[0], base, base + 1, base + 2));
  return state.poseCache.get(k);
}
const poseKey = (l) => l.ox + ':' + l.oy;
function poseAdjGet(g, l) { const m = state.poseAdj && state.poseAdj.get(g), v = l && m && m.get(poseKey(l)); return v ? v.d : [0, 0]; }
function firstPose(ps) { const i = ps.findIndex((p) => p.eye && p.eye.sub === 0 && p.mouth && p.mouth.sub === 0); return i >= 0 ? i : Math.max(0, ps.findIndex((p) => p.eye && p.eye.sub === 0)); }
function eyePreview(g) {
  if (face.g !== g) { face.g = g; face.small = false; face.pose = firstPose(posesOf(g, false)); }
  const ps = posesOf(g, face.small), only = $('dPoseOnly').checked, cv = $('dEyeCv'), x = cv.getContext('2d');
  const ge = (state.eyeShift && state.eyeShift.get(g)) || [0, 0], gm = (state.mouthShift && state.mouthShift.get(g)) || [0, 0];
  const sc = (v) => (face.small ? [half(v[0]), half(v[1])] : v);
  $('dFaceBig').classList.toggle('on', !face.small); $('dFaceSmall').classList.toggle('on', face.small);
  $('dFaceHint').textContent = t(only ? 'poseOnlyHint' : 'eyeMvHint');
  x.clearRect(0, 0, 64, 64);
  let f;
  if (ps.length) { face.pose = Math.min(Math.max(0, face.pose), ps.length - 1); f = ps[face.pose]; }
  else { const o = C.eyeFirst(state.ctx.files[0], g, g + 1); if (!o) return; f = { bs: o.bs, eye: { sub: o.es, dx: o.dx, dy: o.dy, ox: -1, oy: -1 }, mouth: null }; }
  $('dPoseNr').textContent = ps.length ? t('poseNr', { i: face.pose + 1, n: ps.length }) : '';
  $('dPosePrev').disabled = $('dPoseNext').disabled = ps.length < 2;
  $('dPoseOnly').disabled = !ps.length;
  $('dMouthRow').hidden = !f.mouth;
  const ea = poseAdjGet(g, f.eye), ma = poseAdjGet(g, f.mouth);
  $('dEyeVal').textContent = shTxt(only ? ea : ge); $('dMouthVal').textContent = shTxt(only ? ma : gm);
  const base = face.small ? g + 3 : g, B = decode(base), b = B.subs[f.bs] || B.subs[0];
  const img = new ImageData(new Uint8ClampedArray(b.data), b.w, b.h);
  const put = (part, lay, sh) => {
    if (!lay) return; let P; try { P = decode(base + part); } catch (e) { return; }
    const e = P.subs[lay.sub] || P.subs[0]; if (!e) return;
    const ox = lay.dx + ((b.w - e.w) >> 1) + sh[0], oy = lay.dy + ((b.h - e.h) >> 1) + sh[1];
    for (let y = 0; y < e.h; y++) for (let xx = 0; xx < e.w; xx++) {
      const s = (y * e.w + xx) * 4; if (e.data[s + 3] < 128) continue;
      const X = xx + ox, Y = y + oy; if (X < 0 || Y < 0 || X >= b.w || Y >= b.h) continue;
      const t = (Y * b.w + X) * 4; img.data[t] = e.data[s]; img.data[t + 1] = e.data[s + 1]; img.data[t + 2] = e.data[s + 2]; img.data[t + 3] = 255;
    }
  };
  const se = sc(ge), sm = sc(gm);
  put(1, f.eye, [se[0] + ea[0], se[1] + ea[1]]); put(2, f.mouth, [sm[0] + ma[0], sm[1] + ma[1]]);
  cv.width = b.w; cv.height = b.h; x.putImageData(img, 0, 0);
}
const clamp16 = (v) => Math.max(-16, Math.min(16, v));
function curLayer(which) { const ps = posesOf(face.g, face.small); const f = ps[face.pose]; return f ? f[which] : null; }
function faceMove(which, dx, dy, reset) {
  if (!state.open || state.open.kind !== 'fig') return;
  const g = state.open.idx;
  if ($('dPoseOnly').checked) {
    const l = curLayer(which); if (!l || l.ox < 0) return;
    if (!state.poseAdj) state.poseAdj = new Map();
    const m = state.poseAdj.get(g) || new Map(), k = poseKey(l), c = (m.get(k) || { d: [0, 0] }).d, n = reset ? [0, 0] : [clamp16(c[0] + dx), clamp16(c[1] + dy)];
    if (n[0] || n[1]) m.set(k, { ox: l.ox, oy: l.oy, d: n }); else m.delete(k);
    if (m.size) state.poseAdj.set(g, m); else state.poseAdj.delete(g);
  } else {
    const key = which === 'eye' ? 'eyeShift' : 'mouthShift';
    if (!state[key]) state[key] = new Map();
    const c = state[key].get(g) || [0, 0], n = reset ? [0, 0] : [clamp16(c[0] + dx), clamp16(c[1] + dy)];
    if (n[0] || n[1]) state[key].set(g, n); else state[key].delete(g);
  }
  eyePreview(g); renderPlan();
}
document.querySelectorAll('[data-eye]').forEach((bt) => bt.onclick = () => { const [dx, dy] = bt.dataset.eye.split(',').map(Number); faceMove('eye', dx, dy); });
document.querySelectorAll('[data-mouth]').forEach((bt) => bt.onclick = () => { const [dx, dy] = bt.dataset.mouth.split(',').map(Number); faceMove('mouth', dx, dy); });
$('dEyeReset').onclick = () => faceMove('eye', 0, 0, true);
$('dMouthReset').onclick = () => faceMove('mouth', 0, 0, true);
const poseStep = (k) => { if (!state.open) return; const n = posesOf(face.g, face.small).length; if (n) face.pose = (face.pose + k + n) % n; eyePreview(state.open.idx); };
$('dPosePrev').onclick = () => poseStep(-1); $('dPoseNext').onclick = () => poseStep(1);
const faceSize = (small) => { if (!state.open) return; face.small = small; face.pose = firstPose(posesOf(face.g, small)); eyePreview(state.open.idx); };
$('dFaceBig').onclick = () => faceSize(false); $('dFaceSmall').onclick = () => faceSize(true);
$('dPoseOnly').onchange = () => { if (state.open) eyePreview(state.open.idx); };
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
const half = (v) => Math.sign(v) * Math.round(Math.abs(v) / 2);   // symmetric: -1 and 1 both move one pixel
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
  // 1.6.8: eyes moved in the drawer (small figure: half the way)
  for (const [g, [dx, dy]] of state.mouthShift || []) {   // mouth: the same way as the eyes
    dp = C.shiftEyes(dp, g, g + 2, dx, dy).dataPack; dp = C.shiftEyes(dp, g + 3, g + 5, half(dx), half(dy)).dataPack;
    any = true;
  }
  for (const [, m] of state.poseAdj || []) { if (m.size) { dp = C.layersMove(dp, [...m.values()]); any = true; } }   // single poses
  for (const [g, [dx, dy]] of state.eyeShift || []) {
    dp = C.shiftEyes(dp, g, g + 1, dx, dy).dataPack; dp = C.shiftEyes(dp, g + 3, g + 4, half(dx), half(dy)).dataPack;
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
  try { const raw = new Uint8Array(await f.arrayBuffer()); state.dim = C.smaDetect(raw) ? C.smaLoad(raw) : C.dimLoad(raw); state.dimSel = undefined; renderDim(); if (!state.dim.chars.length) toast(t('notDim', { e: t('cardNoChars') }), true); }
  catch (err) { toast(t('notDim', { e: em(err) }), true); }
};

// ------------------------------------------------------------ overview
/* 1.6.8: one chip for all emulators, the main one (A and C, or the first entry) in bold */
function planEmus(emuOn, kind) {
  const em = state.emu, xs = state.extras || [];
  const oldNm = () => (F.embedded() && F.feldApps(state.ctx.rom, F.embedded().stub).name0) || mainName(state.ctx.rom) || 'EMU';
  const mainNm = em ? emuNm(em) : emuOn ? oldNm() : '';
  const v = [];
  if (mainNm) v.push([mainNm, !doomMain()]);   // with DOOM as the main program the emulator goes to the menu
  if (!em && kind === 2 && state.emuRemove) v.push([oldNm() + ' ' + t('stDel'), false]);
  xs.forEach((x) => v.push([(v.length ? ', ' : '') + E.extraName(x), false]));
  if (!v.length) return [t('planEmus'), t('stNone'), 'off'];
  const isNew = !!em || !!state.extrasDirty, upd = emuUpPending() || emuAutoDirty() || !!state.extrasUp, gone = !em && kind === 2 && state.emuRemove;
  if (isNew || upd || !gone) v.push([' · ' + (isNew ? t('stNew') : upd ? t('stUpd') : t('stKeep')), false]);
  return [t('planEmus'), v, isNew || upd ? 'new' : gone ? 'del' : 'keep'];
}
const MB = (n) => (n / 1048576).toFixed(n < 1048576 ? 2 : 1).replace(/\.?0+$/, '') + ' MB';
function renderPlan() {
  const el = $('plan'); if (!el || !state.ctx) return;
  const rom = state.ctx.rom, kind = appInstalled();
  const emuOn = !!state.emu || (kind === 2 && !state.emuRemove && !state.doom), doomOn = !!state.doom || (kind === 1 && !state.emuRemove && (!state.emu || !!state.launch));
  const fe = F.embedded(), menuOld = fe && F.feldFind(rom, fe.stub);
  const st = (on, isNew, old, extra) => on ? (isNew ? t('stNew') : t('stKeep')) + (extra ? ', ' + extra : '') : (old ? t('stDel') : t('stNone'));
  const chips = [
    [t('planFig'), state.changed.size ? t('stChanged', { n: state.changed.size }) : t('stSame'), state.changed.size ? 'new' : 'keep'],
    planEmus(emuOn, kind),
    [t('planDoom'), st(doomOn, !!state.doom, kind === 1, state.doom ? 'E' + $('doomEp').value + ($('doomMusic').checked ? ' + ' + t('doomMusic') : '') : ''), state.doom ? 'new' : doomOn ? 'keep' : kind === 1 ? 'del' : 'off'],
  ];
  const menuOn = menuWanted(), parts = [state.launch ? t('planLaunch') : '', state.feld ? t('feldOn') : '', state.bag ? t('bagOn') : '', state.tama ? t('tamaOn') : ''].filter(Boolean).join(', ');
  const menuSame = state.feld === state.feldInit && state.bag === state.bagInit && state.tama === state.tamaInit && state.saves === state.savesInit && state.launch === state.launchInit;
  chips.push([t('planMenu'), menuOn ? parts + (state.feldStale && menuSame ? ' (' + t('stUpd') + ')' : '') : (menuOld ? t('stDel') : t('stNone')), menuOn ? ((menuSame && !state.feldStale) ? 'keep' : 'new') : menuOld ? 'del' : 'off']);
  const sixIn = !!S6.sixInfo(rom);
  { const n = new Set([...(state.eyeShift || new Map()).keys(), ...(state.mouthShift || new Map()).keys(), ...(state.poseAdj || new Map()).keys()]).size; if (n) chips.push([t('planEyes'), t('planNamesN', { n }), 'new']); }
  if (state.texts && state.texts.size) chips.push([t('planNames'), t('planNamesN', { n: state.texts.size }), 'new']);
  if (sixIn || state.six || state.sixWant) chips.push([t('planSix'), sixIn ? t('stKeep') : t('stNew'), sixIn ? 'keep' : 'new']);
  // Flash: from 0x111000 to the end
  const base = 0x111000, end = 0x1000000, total = end - base, segs = [];
  const assets = estimateEnd() - base + (state.six && !sixIn ? 0xB9000 : 0); segs.push(['fw', t('segFw'), assets]);
  let doomSize = 0, emuSize = 0;
  if (state.doom) { const f = state.doom.fit[$('doomEp').value]; doomSize = (f ? ($('doomMusic').checked && f.music ? f.sizeM : f.sizeP) : 0) + (0xD48000 - 0xCA0000); }
  else if (doomOn) doomSize = (rom[0x830000] | rom[0x830001] << 8 | rom[0x830002] << 16 | rom[0x830003] << 24) + (0xD48000 - 0xCA0000);
  if (state.emu) emuSize = state.emu.size || 0;
  else if (emuOn) { const c7 = E.t7Cfg(rom), c18 = E.t18Cfg(rom); if (c18) emuSize = c18.map.filter((b) => b !== 0xFFFF).length << 16; else if (c7) emuSize = c7.romLen + c7.figs.reduce((n, f) => n + f.len, 0); else { const o = 0xA0000 + 0x140 + 12; emuSize = rom[o] | rom[o + 1] << 8 | rom[o + 2] << 16 | rom[o + 3] << 24; } }
  for (const x of state.extras || []) emuSize += E.extraSize(x);   // more emulators: program, save, data
  if (doomOn) segs.push(['doom', t('segDoom'), doomSize]);
  if (emuOn || (state.extras && state.extras.length)) segs.push(['emu', t('segEmu'), emuSize]);
  if (state.feld && !doomOn) { const ed = F.edition(rom); segs.push(['slots', t('segSlots'), ed ? ed.profile.names.length * 0x58000 : 0]); }
  const save = (state.launch ? 0xF00000 : end) - 0xD29000; segs.push(['save', t('segSave'), save]);   // start device: the MB behind the save is free for programs
  const used = segs.reduce((a, x) => a + x[2], 0); segs.push(['free', t('segFree'), Math.max(0, total - used)]);
  el.innerHTML = '';
  const h = document.createElement('div'); h.className = 'plan-h'; h.textContent = t('planTitle'); el.appendChild(h);
  const row = document.createElement('div'); row.className = 'chips';
  chips.forEach(([l, v, c]) => {
    const d = document.createElement('span'); d.className = 'chip ' + c; d.innerHTML = '<b></b> <span></span>'; d.firstChild.textContent = l;
    if (Array.isArray(v)) for (const [txt, bold] of v) { const e = document.createElement(bold ? 'strong' : 'span'); e.textContent = txt; d.lastChild.appendChild(e); }   // 1.6.8: the main emulator in bold
    else d.lastChild.textContent = v;
    row.appendChild(d);
  });
  el.appendChild(row);
  const bar = document.createElement('div'); bar.className = 'flash';
  const leg = document.createElement('div'); leg.className = 'legend';
  segs.forEach(([k, l, n]) => {
    if (n <= 0) return;
    const i = document.createElement('i'); i.className = 'seg ' + k; i.style.width = (n / total * 100).toFixed(2) + '%'; i.title = l + ': ' + MB(n); bar.appendChild(i);
    const g = document.createElement('span'); g.innerHTML = '<i class="dot ' + k + '"></i>'; g.appendChild(document.createTextNode(l + ' ' + MB(n))); leg.appendChild(g);
  });
  el.appendChild(bar); el.appendChild(leg);
  const r = document.createElement('div'); r.className = 'rule'; r.textContent = state.launch ? t('ruleLaunch') : appRoom() ? t('ruleBoth') : t('ruleNoRoom'); el.appendChild(r);
}

// ------------------------------------------------------------ emulator
function appInstalled() {
  try {
    const r = state.ctx.rom, fe = F.embedded();
    if (fe && F.isLaunch(r, fe.stub)) return F.appKind(r);   // 1.6.8: start device, the home screen hooks point to the trampolines
    return E.installedOrigs(r, E.sceneTable(r)) ? F.appKind(r) : 0;
  } catch (e) { return 0; }
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
function emuAutoDirty() { return (emuInstalled() || (doomInstalled() && !state.launch)) && !state.emu && !state.emuRemove && !state.doom && (!!E.autoGet(state.ctx.rom, stubList(state.ctx.rom)) !== !!state.emuAuto || (emuInstalled() && emuClockDirty())); }
function emuClockDirty() { const c = E.emuClockGet(state.ctx.rom), n = E.emuNfcGet(state.ctx.rom), o = E.emuOptGet(state.ctx.rom), w = state.emuOpt || {}; return (c !== null && c !== !!state.emuClock) || (n !== null && n !== (state.emuNfc !== false)) || (!!o && (o.vol !== w.vol || o.dim !== w.dim || o.off !== w.off)); }
function emuUpPending() {
  const emb = E.embedded();
  if (state.emu || state.emuRemove || state.doom || !emb || !emuInstalled()) return false;
  try {
    if (F.emuType(state.ctx.rom) === 't7') return !!(E.t7Embedded() && E.t7Upgrade(state.ctx.rom, E.t7Embedded()));
    if (F.emuType(state.ctx.rom) === 'v1') return !!(E.v1Embedded() && E.v1Upgrade(state.ctx.rom, E.v1Embedded()));
    if (F.emuType(state.ctx.rom) === 't18') return !!(E.t18Embedded() && E.t18Upgrade(state.ctx.rom, E.t18Embedded(), E.embedded() && E.embedded().stub));
    return !!E.emuUpgrade(state.ctx.rom, emb.app);
  } catch (e) { return false; }
}
/* Room for emulator and DOOM: both need the area after the characters from 0x830000 and the program space at 0x0A0000 */
/* 1.6.9 test25: firmware that reaches past 0x0A0000 (Jade Forest wave 2 up to 0x0A1683, GitHub issue; Tropical, Glacier):
 * no main program there, but a start device puts every program into the menu (appsPlace, anywhere free) */
function a0Free() { const r = state.ctx.rom; return !!F.appKind(r) || r.subarray(0xA0000, 0xA1000).every(b => b === 0xFF); }
function appRoom() { return !a0Free() || estimateEnd() <= 0x830000; }   // without a main program the trial build decides (roomMissing)
function renderEmu() {
  if (!state.ctx) return;
  renderGb();
  const has = emuInstalled();
  $('btnEmuOff').hidden = true; $('btnEmuUndo').hidden = true;   // 1.6.8: in the list of programs (×, ↺)
  let s;
  if (!E.embedded()) s = t('emuMissing');
  const t7In = has && F.emuType(state.ctx.rom) === 't7', t7 = state.emu ? !!state.emu.t7 : t7In && !state.emuRemove;
  const t18In = has && F.emuType(state.ctx.rom) === 't18', t18 = state.emu ? !!state.emu.t18 : t18In && !state.emuRemove;
  const t7Fr = state.emu ? !!state.emu.t7 && E.t7Kind(state.emu.rom) === 'friends' : t7In && !!E.t7Cfg(state.ctx.rom) && E.t7Cfg(state.ctx.rom).romLen === 0xC0000;
  if (state.emu) s = state.emu.v1 ? t(E.v3Detect(state.emu.rom) ? 'v3Ok' : 'v1Ok') : state.emu.t7 ? t(t7Fr ? 't7OkFr' : 't7Ok') : state.emu.t18 ? t('t18Ok') : t('emuOk', { size: E.DEVICES[state.emu.dev].label });
  else if (state.emuRemove) s = t('emuGone');
  else s = has ? t(t7In ? (t7Fr ? 't7HasFr' : 't7Has') : t18In ? 't18Has' : 'emuHas') + (emuUpPending() ? ' ' + t('emuUp') : '') : (state.extras && state.extras.length ? t('emuInList', { n: state.extras.map((x) => E.extraName(x)).join(', ') }) : t('emuNone'));
  const v1 = state.emu ? !!state.emu.v1 : has && F.emuType(state.ctx.rom) === 'v1' && !state.emuRemove;
  const v3 = v1 && E.v3Detect(state.emu ? state.emu.rom : E.v1FromRom(state.ctx.rom));
  s += (state.emu || (has && !state.emuRemove) ? '\n' + t(v3 ? 'v3Use' : v1 ? 'v1Use' : t7 ? 't7Use' : t18 ? 't18Use' : 'emuUse') : '');
  const bigIn = has && !state.emu && !state.emuRemove && state.ctx.rom[0xA0148] === 0 && state.ctx.rom[0xA014A] === 0x84;
  if ((state.emu && state.emu.big) || bigIn) s += '\n' + t('emuBig');
  const room = appRoom();
  $('btnEmu').disabled = !room;
  if (!room) s = t('noRoomApp');
  else if (!a0Free()) s += '\n' + t(state.launch ? 'a0Menu' : 'a0MenuN');
  else if (!state.launch && !state.emu && (state.doom || (doomInstalled() && !state.emuRemove))) s += '\n' + t('emuVsDoom');
  $('emuStat').textContent = s;
  renderT7Figs(t7 && room && !t7Fr);
  renderApps();
  $('emuAutoRow').hidden = true; $('emuClockRow').hidden = true;   // 1.6.8: in the main program's row
  { const is4u = state.emu ? (state.emu.dev === '4u' || state.emu.dev === '4u-plain') : (!state.emuRemove && E.emuNfcGet(state.ctx.rom) !== null); $('emuNfcRow').hidden = true; void is4u; $('emuNfc').checked = state.emuNfc !== false; }
  $('emuAuto').checked = !!state.emuAuto;
  $('emuClock').checked = state.emuClock !== false;
  renderPlan();
  $('emuDlc').hidden = true;   // 1.6.8: settings, figures and download slots open per program in the list (▾)
  $('btnT7Fig').hidden = true;
}
/* T7Emu figures: list with remove buttons */
function t7State() {
  if (state.emu && state.emu.t7) return state.emu;
  const cur = E.t7FromRom(state.ctx.rom); if (!cur) return null;
  return { t7: true, dev: 't7', rom: cur.rom, figs: cur.figs, size: 0 };
}
function t7Size(st) { return st.rom.length + st.figs.reduce((n, f) => n + f.data.length, 0); }
/* 1.6.8: download items and figures also for the programs in the menu. Target: 'main' or the index in state.extras */
const DLC_DEV = ['idl', 'ps', '4u', '4u-plain'];
/* tg: 'main' or the index in state.extras (default: the file dialog's target state.dlcT) */
function dlcTarget(tg = state.dlcT) {
  if (typeof tg === 'number') { const x = (state.extras || [])[tg]; return x && x.kind === 'tama' && DLC_DEV.includes(x.dev) ? { guest: x.guest, dev: x.dev, set: (g) => { x.guest = g; state.extrasDirty = true; } } : null; }
  const em = state.emu;
  if (em) return !em.t7 && !em.t18 && !em.v1 && DLC_DEV.includes(em.dev) ? { guest: em.guest, dev: em.dev, set: (g) => { em.guest = g; } } : null;
  // the installed main TamaEmu: its firmware with the save, read from the ROM; it goes into the work on the first change
  const mg = state.mainGuest;
  if (mg && !state.emuRemove) return { guest: mg.g, dev: mg.dev, set: (g) => { const pk = E.packGuest(g, mg.dev); state.emu = { guest: g, dev: mg.dev, size: pk.blob.length, big: pk.big, kept: true }; state.mainGuest = null; } };
  return null;
}
/* test5: Gotchi Points of a Tama-Go (T7Emu): EEPROM 0x030-0x034, 5 bytes BCD lowest digits first, no checksum
 * (save.bin: behind the 2 byte page mask). Goes in like an imported save: the running state starts anew. */
function t7GpSave(tg) {
  const rom = state.ctx.rom;
  if (tg === 'main') return state.t7save || SV.t7SaveOut(rom);
  const x = state.extras[tg]; if (!x || x.kind || x.fresh) return null;
  if (state.t7saveX && state.t7saveX.i === tg) return state.t7saveX.save;
  const o = romExtras(rom).find((r) => E.extraKind(r) === 't7' && r.rom.length === x.rom.length && r.rom.every((v, k) => v === x.rom[k]));
  return o ? SV.t7SaveOut(rom, o.saveOff) : null;
}
function t7GpGet(sv) {
  let n = 0;
  for (let i = 4; i >= 0; i--) { const v = sv[0x32 + i], hi = v >> 4, lo = v & 15; if (hi > 9 || lo > 9) return -1; n = n * 100 + hi * 10 + lo; }
  return n;
}
function t7GpInto(box, tg) {
  const sv = t7GpSave(tg); if (!sv || !(sv[0] & 1)) return;   // page 0 of the EEPROM must be there
  const n = t7GpGet(sv); if (n < 0) return;
  const l = document.createElement('label'); l.className = 'check'; const sp = document.createElement('span'); sp.textContent = t('t7Gp') + ' ';
  const inp = document.createElement('input'); inp.type = 'number'; inp.min = 0; inp.max = 999999; inp.value = String(n); inp.style.width = '7em';
  inp.onchange = () => {
    let v = Math.max(0, Math.min(999999, Math.floor(+inp.value || 0)));
    const g = new Uint8Array(sv);
    for (let i = 0; i < 5; i++) { const d = v % 100; g[0x32 + i] = ((d / 10 | 0) << 4) | (d % 10); v = Math.floor(v / 100); }
    if (tg === 'main') state.t7save = g; else state.t7saveX = { i: tg, save: g };
    renderPlan(); renderEmu();
  };
  l.append(sp, inp); box.appendChild(l);
  const w = document.createElement('div'); w.className = 'hint'; w.textContent = t('t7GpHint'); box.appendChild(w);
}
/* 1.6.8 (test47): items of T18Emu (MyMeets files) go straight into its firmware's save; the running state starts anew */
function t18Target(tg) {
  if (typeof tg === 'number') { const x = (state.extras || [])[tg]; return x && x.kind === 't18' ? { fw: x.fw, set: () => { x.items = true; state.extrasDirty = true; } } : null; }
  const em = state.emu;
  if (em) return em.t18 ? { fw: em.rom, set: () => { em.items = true; } } : null;
  if (!emuInstalled() || state.emuRemove || F.emuType(state.ctx.rom) !== 't18') return null;
  if (!state.t18Main) state.t18Main = E.t18FromRom(state.ctx.rom);
  const fw = state.t18Main; if (!fw) return null;
  return { fw, set: () => { state.t18Dirty = true; } };   // written into the installed program on save (E.t18RomWriteGuest)
}
function picCanvas(pic, sc) {
  const c = document.createElement('canvas'); c.className = 'itempic';
  if (!pic) { c.width = c.height = 1; return c; }
  c.width = pic.w; c.height = pic.h; c.style.width = (pic.w * sc) + 'px'; c.style.height = (pic.h * sc) + 'px';
  c.getContext('2d').putImageData(new ImageData(pic.rgba, pic.w, pic.h), 0, 0); return c;
}
/* test47: pictures for "grow up" (the user's own files: <name>.png, <id>.png or the Android T18Emu set with erwachsen.json) */
function t18PicsInto(box) {
  const h = document.createElement('h4'); h.className = 'subhead'; h.textContent = t('t18Pics'); box.appendChild(h);
  const tools = document.createElement('div'); tools.className = 'tools';
  const pick = document.createElement('button'); pick.className = 'btn small'; pick.textContent = t('t18PicsPick'); pick.onclick = () => $('fileT18Pic').click();
  tools.appendChild(pick);
  if (state.t18Pics && state.t18Pics.size) { const del = document.createElement('button'); del.className = 'btn small'; del.textContent = t('t18PicsDel'); del.onclick = () => { state.t18Pics = new Map(); state.t18PicsDirty = true; renderEmu(); }; tools.appendChild(del); }
  box.appendChild(tools);
  if (state.t18Pics && state.t18Pics.size) {
    const g = document.createElement('div'); g.className = 'picgrid';
    for (const [id, p] of state.t18Pics) { const f = document.createElement('figure'); const c = picCanvas(E.t18PicRgba(p), 1); const cap = document.createElement('figcaption'); cap.textContent = (E.T18_ADULTS.find((a) => a[0] === id) || [0, '#' + id])[1]; f.append(c, cap); g.appendChild(f); }
    box.appendChild(g);
  }
  const w = document.createElement('div'); w.className = 'hint'; w.textContent = t('t18PicsHint'); box.appendChild(w);
}
async function pngRgba(bytes) {
  const bm = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
  const c = document.createElement('canvas'); c.width = bm.width; c.height = bm.height; const x = c.getContext('2d'); x.drawImage(bm, 0, 0);
  return { w: bm.width, h: bm.height, rgba: x.getImageData(0, 0, bm.width, bm.height).data };
}
/* test3: items or Tamapoints changed: with "keep saves" the device would bring back its own T18 save (session, sector
 * tables, spare blocks) over the changed one. The whole T18 save comes from the image then (it is the device's state at
 * reading plus the change). */
function t18Fresh(rom, off, fresh) {
  const c = E.extraCfg(rom, off); if (!c || c.kind !== 't18') return;
  fresh.push([c.saveOff, c.saveOff + 0x24000]);
  for (const b of c.spare) if (b !== 0xFFFF) fresh.push([b << 16, (b + 1) << 16]);
}
/* test2: a name the Studio cannot read (Japanese letters) shows as its number */
function t18Nm(r) { return r.readable !== false || !r.cat ? r.name : t('t18ItemNo', { n: r.cat }); }
/* test6: MixingMeets for T18Emu on the Paradise. The page (mixing-meets.com, Connect and Return) speaks Web Bluetooth
 * with "TMGC_meets" (service fff0, fff1 notify, fff2 write). This bookmarklet replaces navigator.bluetooth on that page:
 * "Select Tamagotchi" then opens the serial port of the UART dongle on the Paradise's contacts, and T18Emu hands the data
 * to the firmware's Bluetooth module, as the Android T18Emu does. Frames: see t18/src/app/main.c (T18_LINK). */
function pmxBridge() {
  if (window.__pmxBridge) { alert('Paramix: bridge already on'); return; }
  if (!navigator.serial) { alert('Paramix: this browser has no Web Serial (Chrome or Edge on a computer)'); return; }
  window.__pmxBridge = 1;
  const bn = document.createElement('div');
  bn.style.cssText = 'position:fixed;left:8px;bottom:8px;z-index:2147483647;background:#1d2b44;color:#fff;padding:8px 12px;border-radius:8px;font:13px/1.3 sans-serif;max-width:320px';
  const st = (m) => { bn.textContent = 'Paramix: ' + m; };
  st('bridge on. Select Tamagotchi, then choose the dongle.'); document.body.appendChild(bn);
  const FFF0 = '0000fff0-0000-1000-8000-00805f9b34fb', FFF1 = '0000fff1-0000-1000-8000-00805f9b34fb', FFF2 = '0000fff2-0000-1000-8000-00805f9b34fb';
  let port = null, reader = null, writer = null, hello = 0, ack = null, ping = null, live = false;
  const notes = [], gone = [];
  let fs = 0, fty = 0, flen = 0, fbuf = [], fsum = 0;
  const frame = (ty, d) => { const a = new Uint8Array(4 + d.length); a[0] = 0x5A; a[1] = ty; a[2] = d.length; let s = ty + d.length; for (let i = 0; i < d.length; i++) { a[3 + i] = d[i]; s += d[i]; } a[3 + d.length] = s & 255; return a; };
  const send = (ty, d) => writer.write(frame(ty, d || []));
  const R = { uuid: FFF1, properties: { notify: true, read: false, write: false, writeWithoutResponse: false }, value: null,
    addEventListener: (ty, fn) => { if (ty === 'characteristicvaluechanged') notes.push(fn); }, removeEventListener: () => {},
    startNotifications: async () => R, stopNotifications: async () => R };
  const got = (ty, d) => {
    if (ty === 0x48) { hello = Date.now(); return; }
    if (ty === 0x4B) { const a = ack; ack = null; if (a) a(); return; }
    if (ty === 0x4E) { const u = new Uint8Array(d); R.value = new DataView(u.buffer); for (const fn of notes) { try { fn({ target: R }); } catch (e) { console.error(e); } } }
  };
  const feed = (b) => {
    if (fs === 0) { if (b === 0xA5) fs = 1; return; }
    if (fs === 1) { fty = b; fsum = b; fs = 2; return; }
    if (fs === 2) { if (b > 64) { fs = 0; return; } flen = b; fsum += b; fbuf = []; fs = flen ? 3 : 4; return; }
    if (fs === 3) { fbuf.push(b); fsum += b; if (fbuf.length >= flen) fs = 4; return; }
    fs = 0; if ((fsum & 255) === b) got(fty, fbuf);
  };
  const down = () => { if (!live) return; live = false; dev.gatt.connected = false; clearInterval(ping); for (const fn of gone) { try { fn({ target: dev }); } catch (e) {} } };
  const readLoop = async () => { try { for (;;) { const { value, done } = await reader.read(); if (done) break; for (const b of value) feed(b); } } catch (e) {} st('dongle closed'); down(); };
  const write = async (data) => {
    if (!live) throw new Error('Your Tama disconnected');
    const u = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    for (let o = 0; o < u.length; o += 60) {
      const p = new Promise((res, rej) => { ack = res; setTimeout(() => { if (ack === res) { ack = null; rej(new Error('Paradise did not answer')); } }, 15000); });
      await send(0x57, Array.from(u.subarray(o, o + 60))); await p;
    }
  };
  const T = { uuid: FFF2, properties: { write: true, writeWithoutResponse: false, notify: false, read: false },
    writeValueWithResponse: write, writeValue: write, writeValueWithoutResponse: write };
  const svc = { uuid: FFF0, isPrimary: true, getCharacteristic: async (u) => { u = String(u).toLowerCase(); if (u.indexOf('fff1') >= 0) return R; if (u.indexOf('fff2') >= 0) return T; throw new Error('No characteristic ' + u); } };
  const dev = { id: 'paramix', name: 'TMGC_meets', addEventListener: (ty, fn) => { if (ty === 'gattserverdisconnected') gone.push(fn); }, removeEventListener: () => {} };
  dev.gatt = { device: dev, connected: false,
    connect: async () => { await send(0x43, [1]); live = true; dev.gatt.connected = true; clearInterval(ping); ping = setInterval(() => { send(0x50, []).catch(() => {}); }, 1000); st('connected to the Paradise'); return dev.gatt; },
    disconnect: () => { if (writer) send(0x43, [0]).catch(() => {}); down(); },
    getPrimaryService: async (u) => { if (String(u).toLowerCase().indexOf('fff0') < 0) throw new Error('No services matching UUID ' + u); return svc; } };
  const requestDevice = async () => {
    if (!port) {
      const pp = await navigator.serial.requestPort();
      try { await pp.open({ baudRate: 460800, bufferSize: 4096 }); }
      catch (e) { st('the dongle is busy. In Paramix Studio press "UART freigeben" (or close the Studio), then try again.'); alert('Paramix: the dongle is busy.\nParamix Studio still holds it: press "UART freigeben" there or close the Studio, then try again.'); throw e; }
      port = pp;
      reader = port.readable.getReader(); writer = port.writable.getWriter(); readLoop();
    }
    st('looking for T18Emu on the Paradise...');
    const t0 = Date.now(); while (!hello && Date.now() - t0 < 4000) await new Promise((r) => setTimeout(r, 100));
    if (!hello) { st('no answer from the Paradise'); throw new Error('Paradise not found: start T18Emu and check the dongle'); }
    st('Paradise found'); return dev;
  };
  try { Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: { requestDevice, getAvailability: async () => true } }); }
  catch (e) { alert('Paramix: could not take over Bluetooth on this page'); }
  window.addEventListener('pagehide', () => { try { if (writer) writer.write(frame(0x43, [0])); } catch (e) {} });
}
function pmxBridgeHref() { return 'javascript:' + encodeURIComponent('(' + pmxBridge.toString() + ')()'); }
window.pmxBridgeHref = pmxBridgeHref;   // for the test
window.pmxTest = { uartWarnInto: (b) => uartWarnInto(b), devFree: () => devFree(), devRender: () => devRender(), dv: () => dv };
function t18BridgeInto(box) {
  const h = document.createElement('h4'); h.className = 'subhead'; h.textContent = t('mmTitle'); box.appendChild(h);
  const a = document.createElement('a'); a.className = 'btn small'; a.href = pmxBridgeHref(); a.textContent = t('mmLink'); a.onclick = (e) => { e.preventDefault(); toast(t('mmDrag')); };
  box.appendChild(a);
  const w = document.createElement('div'); w.className = 'hint'; w.style.whiteSpace = 'pre-line'; w.textContent = t('mmHint'); box.appendChild(w);
  uartWarnInto(box);
  pairInto(box, 'pairHint');
}
/* 1.6.9: visits over the contacts for the other emulators (T7Emu Tama-Go so far) */
const IR_HINT = { ps: 'pairHintPs', idl: 'pairHintIdl' };   /* TamaEmu with the infrared link (1.6.9) */
function linkInto(box, hint) { pairInto(box, hint); }
/* test7: the bridge page needs the dongle for themselves. While the Studio holds it, say so and
 * offer to let it go. */
const uartWarns = [];
function uartWarnRender() {
  for (const b of uartWarns) { if (!b.isConnected) continue; b.hidden = !dv.io; }
}
function uartWarnInto(box) {
  const d = document.createElement('div'); d.className = 'uartwarn';
  const m = document.createElement('span'); m.textContent = t('uartBusy') + ' ';
  const k = document.createElement('button'); k.className = 'btn small'; k.textContent = t('uartFree'); k.onclick = () => devFree();
  d.append(m, k); box.appendChild(d); uartWarns.push(d); d.hidden = !dv.io;
}
async function devFree() {
  if (dv.run) { toast(t('uartRun')); return; }
  try { if (dv.io) await dv.io.close(); } catch (e) {}
  dv.io = null; dv.d = null; dv.loaded = false; devRender(t('devNone')); toast(t('uartFreed'));
}
/* 1.6.9: visits between two Paradise: plugged together at the top, the programs talk over the contacts themselves */
function pairInto(box, hint) {
  const h = document.createElement('h4'); h.className = 'subhead'; h.textContent = t('pairTitle'); box.appendChild(h);
  const w = document.createElement('div'); w.className = 'hint'; w.style.whiteSpace = 'pre-line'; w.textContent = t(hint); box.appendChild(w);
}
function t18ItemsInto(box, tg) {
  const T = t18Target(tg); if (!T) return;
  const it = E.t18Items(T.fw);
  const h = document.createElement('h4'); h.className = 'subhead'; h.textContent = t('t18Items'); box.appendChild(h);
  if (!it.save) { const w = document.createElement('div'); w.className = 'hint'; w.textContent = t('t18ItemsNoSave'); box.appendChild(w); return; }
  const tools = document.createElement('div'); tools.className = 'tools';
  const pick = document.createElement('button'); pick.className = 'btn small'; pick.textContent = t('t18ItemsPick');
  pick.onclick = () => { state.itemT = tg; $('fileT18Item').click(); };
  const pl = document.createElement('label'); pl.className = 'check'; const ps = document.createElement('span'); ps.textContent = t('t18Points') + ' ';
  const pi = document.createElement('input'); pi.type = 'number'; pi.min = 0; pi.max = 65535; pi.value = String(it.points); pi.style.width = '7em';
  pi.onchange = () => { E.t18Points(T.fw, +pi.value || 0); T.set(); renderEmu(); };
  pl.append(ps, pi); tools.append(pick, pl); box.appendChild(tools);
  const grid = document.createElement('div'); grid.className = 'dlcgrid';
  for (const a of it.areas) {
    const d = document.createElement('div'); d.className = 'dlcbox';
    const used = a.slots.filter((x) => x.rec);
    const hh = document.createElement('h4'); hh.textContent = t('t18A_' + a.key) + ' ' + used.length + '/' + (used.length + a.free); d.appendChild(hh);
    for (const x of used) {
      const r = document.createElement('div'); r.className = 'dlcrow';
      const n = document.createElement('span'); n.className = 'itemname'; n.append(picCanvas(x.rec.pic, 1), document.createTextNode(' ' + t18Nm(x.rec)));
      const sel = document.createElement('select'); [1, 2, 3].forEach((v) => { const o = document.createElement('option'); o.value = String(v); o.textContent = v + '×'; sel.appendChild(o); });
      sel.value = String(x.count); sel.onchange = () => { E.t18ItemCount(T.fw, a.key, x.n, +sel.value); T.set(); renderEmu(); };
      const b = document.createElement('button'); b.className = 'btn small'; b.textContent = t('dlcDel');
      b.onclick = () => { E.t18ItemDel(T.fw, a.key, x.n); T.set(); toast(t('dlcGone', { n: t18Nm(x.rec) })); renderEmu(); };
      const ctl = document.createElement('span'); ctl.className = 'itemctl'; ctl.append(sel, b); r.append(n, ctl); d.appendChild(r);
    }
    const f = document.createElement('div'); f.className = 'dlcrow free'; f.textContent = t('dlcFreeN', { n: a.free }); d.appendChild(f);
    grid.appendChild(d);
  }
  { const d = document.createElement('div'); d.className = 'dlcbox';
    const hh = document.createElement('h4'); hh.textContent = t('t18A_room'); d.appendChild(hh);
    const r = document.createElement('div'); r.className = 'dlcrow';
    if (it.room) {
      const n = document.createElement('span'); n.className = 'itemname'; n.append(picCanvas(it.room.pic, 0.5), document.createTextNode(' ' + t18Nm(it.room)));
      const b = document.createElement('button'); b.className = 'btn small'; b.textContent = t('t18RoomBack');
      b.onclick = () => { E.t18RoomReset(T.fw); T.set(); renderEmu(); };
      r.append(n, b);
    } else r.textContent = t('t18RoomBuiltin');
    d.appendChild(r); grid.appendChild(d); }
  box.appendChild(grid);
  const w = document.createElement('div'); w.className = 'hint'; w.textContent = t('t18ItemsHint'); box.appendChild(w);
}
function figTarget(tg = state.figT) {
  if (typeof tg === 'number') { const x = (state.extras || [])[tg]; if (!x || x.kind || E.t7Kind(x.rom) !== 'go') return null; x.figs = x.figs || []; return x; }
  return t7State();
}
async function mainGuestLoad() {
  if (state.mainGuest) return true;
  const cfg = romEmuCfg(state.ctx.rom); if (!cfg) return false;
  const g = await SV.emuGuestAsync((a, n) => state.ctx.rom.subarray(a, a + n), cfg);
  state.mainGuest = { g, dev: cfg.dev };
  return true;
}
function renderT7Figs() { $('btnT7Fig').hidden = true; $('t7Figs').hidden = true; }   // 1.6.8: the figures are in the program's row
/* list of the figures of one T7Emu, each with × */
function figsInto(box, tg) {
  const st = figTarget(tg); if (!st) return;
  if (!st.figs.length) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = t('t7FigNone'); box.appendChild(d); return; }
  const wrap = document.createElement('div'); wrap.className = 'chips';
  st.figs.forEach((f, i) => {
    const c = document.createElement('span'); c.className = 'chip keep'; c.textContent = E.t7FigName(f.name) + ' ';
    const x = document.createElement('button'); x.className = 'btn small'; x.textContent = '×';
    x.onclick = () => { const s2 = figTarget(tg); s2.figs.splice(i, 1); if (typeof tg === 'number') state.extrasDirty = true; else { s2.size = t7Size(s2); state.emu = s2; emuRemoveClear(); } renderEmu(); };
    c.appendChild(x); wrap.appendChild(c);
  });
  box.appendChild(wrap);
}
/* the download content stores of one TamaEmu, each slot with its name */
function dlcInto(box, tg) {
  const g0 = dlcTarget(tg); if (!g0) return;
  const grid = document.createElement('div'); grid.className = 'dlcgrid';
  for (const st of E.dlcStores(g0.guest)) {
    const used = st.slots.filter(x => x.name).length;
    const d = document.createElement('div'); d.className = 'dlcbox';
    const h = document.createElement('h4'); h.textContent = t('dlcK' + st.kind) + ' ' + used + '/' + st.slots.length; d.appendChild(h);
    for (const x of st.slots) {
      if (!x.name) continue;
      const r = document.createElement('div'); r.className = 'dlcrow';
      const n = document.createElement('span'); n.textContent = x.name;
      const b = document.createElement('button'); b.className = 'btn small'; b.textContent = t('dlcDel');
      b.onclick = () => { const g = dlcTarget(tg); g.set(E.dlcFree(g.guest, st.kind, x.slot)); toast(t('dlcGone', { n: x.name })); renderEmu(); };
      r.append(n, b); d.appendChild(r);
    }
    const f = document.createElement('div'); f.className = 'dlcrow free'; f.textContent = t('dlcFreeN', { n: st.slots.length - used }); d.appendChild(f);
    grid.appendChild(d);
  }
  box.appendChild(grid);
}
function selectInto(box, label, items, val, fn) {
  const r = document.createElement('div'); r.className = 'tools';
  const l = document.createElement('label'); l.className = 'check';
  const sp = document.createElement('span'); sp.textContent = label + ' ';
  const sel = document.createElement('select'); items.forEach((txt, i) => { const o = document.createElement('option'); o.value = String(i); o.textContent = txt; sel.appendChild(o); });
  sel.value = String(val || 0); sel.onchange = () => fn(+sel.value);
  l.append(sp, sel); r.appendChild(l); box.appendChild(r);
}
/* 1.6.8: TamaEmu and T18Emu options (set here, the toy has no menu for them) */
function optInto(box, o, set) {
  selectInto(box, t('optVol'), [t('optVolN'), t('optVolQ'), t('optVolL'), t('optOff')], o.vol, (v) => { o.vol = v; set(); });
  { const U = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0];   // 5 % .. 100 %
    selectInto(box, t('optDim'), U.map((i) => E.OPT_LV[i] + ' %'), Math.max(0, U.indexOf(o.dim)), (v) => { o.dim = U[v]; set(); }); }
  selectInto(box, t('optScr'), [t('optScrToy'), '10 s', '20 s', '30 s', '1 Min', '2 Min', '5 Min', '10 Min'], o.off, (v) => { o.off = v; set(); });
  const h = document.createElement('p'); h.className = 'hint'; h.textContent = t('optDev'); box.appendChild(h);
}
function checkInto(box, label, hint, on, fn) {
  const r = document.createElement('div'); r.className = 'tools';
  const l = document.createElement('label'); l.className = 'check';
  const c = document.createElement('input'); c.type = 'checkbox'; c.checked = on; c.onchange = () => fn(c.checked);
  const sp = document.createElement('span'); sp.textContent = label;
  l.append(c, ' ', sp); r.appendChild(l);
  if (hint) { const h = document.createElement('span'); h.className = 'hint'; h.textContent = hint; r.appendChild(h); }
  box.appendChild(r);
}
/* All programs in one list (1.6.8). Each row opens with ▾: its settings, figures or download slots. */
function renderApps() {
  const box = $('appsList'); box.innerHTML = '';
  const room = appRoom();
  $('btnApp').disabled = !room || !E.t7Embedded();
  state.progOpen = state.progOpen || new Set();
  const row = (key, name, tag, cls, btns, detail) => {
    const r = document.createElement('div'); r.className = 'prow ' + (cls || '');
    const open = !!detail && state.progOpen.has(key);
    if (detail) { const tb = document.createElement('button'); tb.className = 'btn small ptog'; tb.textContent = open ? '▴' : '▾'; tb.title = t('progMore'); tb.onclick = () => { if (open) state.progOpen.delete(key); else state.progOpen.add(key); renderEmu(); }; r.appendChild(tb); }
    const n = document.createElement('span'); n.className = 'pname'; n.textContent = name;
    const g = document.createElement('span'); g.className = 'ptag'; g.textContent = tag;
    r.append(n, g);
    for (const [txt, fn, title, c, off] of btns) { const b = document.createElement('button'); b.className = 'btn small' + (c ? ' ' + c : ''); b.textContent = txt; if (title) b.title = title; if (fn) b.onclick = fn; if (off) b.disabled = true; r.appendChild(b); }
    box.appendChild(r);
    if (open) { const d = document.createElement('div'); d.className = 'pdet'; detail(d); box.appendChild(d); }
  };
  const has = emuInstalled(), em = state.emu, tagMain = t(state.launch ? 'progMainL' : 'progMain');
  // DOOM at 0x0A0000 is the main program then (in a start device the emulators all go to the menu)
  const dm = doomMain();
  const dIn = doomInstalled(), dGone = dIn && !state.doom && (state.emuRemove || (!state.launch && !!state.emu));
  const doomOut = () => { if (state.doom) state.doom = null; else { state.emuRemove = true; if (!state.launch) state.emu = null; } renderEmu(); renderDoom(); };
  const doomBack = () => { state.emuRemove = false; if (!state.launch) state.emu = null; renderEmu(); renderDoom(); };
  // 1.6.9 test13: in the start device DOOM stays at 0x0A0000, but the main program (first in the menu, can start when
  // switched on) can be any program of the list (switch FELD_AUTO in the menu)
  const lp = !!state.launch && dm && mainV9();
  if (lp && state.mainPick && state.mainPick !== 'emu' && !(state.extras || []).includes(state.mainPick)) state.mainPick = null;
  if (lp && state.mainPick === 'emu' && !em) state.mainPick = null;
  const pick = (v) => { state.mainPick = v; state.mainDirty = true; renderEmu(); };
  const autoL = (d) => checkInto(d, t('emuAuto'), t('autoHintL'), !!state.emuAuto, (v) => { state.emuAuto = v; state.mainDirty = true; renderEmu(); });
  const star = (on, v) => on ? ['★', null, t('progIsMain'), 'pstar on', true] : ['☆', () => pick(v), t('progMakeMain'), 'pstar'];
  const doomIsMain = dm && !(lp && state.mainPick);
  if (dm) row('doom', 'DOOM', (doomIsMain ? tagMain : t('progMenu')) + (state.doom ? ' · ' + t('stNew') : ''), doomIsMain ? 'main' : '', [lp ? star(doomIsMain, null) : ['★', null, t('progIsMain'), 'pstar on', true], ['×', doomOut, t('doomOff')]],
    state.launch ? (lp && doomIsMain ? autoL : null) : (d) => checkInto(d, t('emuAuto'), t('doomAutoHint'), !!state.emuAuto, (v) => { state.emuAuto = v; renderEmu(); }));
  else if (dGone) row('doom', 'DOOM', t('progGone'), 'main gone', [['↺', doomBack, t('progUndo')]]);
  const undo = () => { state.emu = null; emuRemoveClear(); state.t7save = null; renderEmu(); renderDoom(); };
  // settings of the main program (they are in the program at 0x0A0000)
  const mainSet = (d, dev) => {
    if (!dm) checkInto(d, t('emuAuto'), t(state.launch && mainV9() ? 'autoHintL' : 'emuAutoHint'), !!state.emuAuto, (v) => { state.emuAuto = v; if (state.launch) state.mainDirty = true; renderEmu(); });
    else if (lp && state.mainPick === 'emu') autoL(d);
    checkInto(d, t('emuClock'), t('emuClockHint'), state.emuClock !== false, (v) => { state.emuClock = v; renderEmu(); });
    if (dev === '4u' || dev === '4u-plain') checkInto(d, t('emuNfc'), t('emuNfcHint'), state.emuNfc !== false, (v) => { state.emuNfc = v; renderEmu(); });
    if (dev !== 't7' && dev !== 'v1' && dev) { state.emuOpt = state.emuOpt || { vol: 0, dim: 0, off: 0 }; optInto(d, state.emuOpt, () => renderEmu()); }
  };
  if (em) {
    const nm = emuNm(em);
    const b = [], go = em.t7 && E.t7Kind(em.rom) === 'go', big = !em.t7 && !em.t18 && !em.v1 && DLC_DEV.includes(em.dev);
    if (go) b.push([t('t7FigPick') + (em.figs.length ? ' (' + em.figs.length + ')' : ''), () => { state.figT = 'main'; $('fileT7Fig').click(); }]);
    if (big) b.push([t('dlcPick'), () => { state.dlcT = 'main'; $('fileDlc').click(); }]);
    const emMain = lp && state.mainPick === 'emu';
    b.push(lp ? star(emMain, 'emu') : dm ? ['☆', null, t('progNoMainDoom'), 'pstar', true] : ['★', null, t('progIsMain'), 'pstar on', true]);
    b.push(['×', undo, t('progUndo')]);
    row('main', nm, dm ? (emMain ? tagMain : t('progMenu')) + ' · ' + t('stNew') : em.kept ? tagMain : tagMain + ' · ' + t('stNew'), dm && !emMain ? '' : 'main', b, (d) => { mainSet(d, em.dev); if (go) { figsInto(d, 'main'); linkInto(d, 'pairHintT7'); } if (big) dlcInto(d, 'main'); if (!em.t7 && !em.t18 && !em.v1 && IR_HINT[em.dev]) linkInto(d, IR_HINT[em.dev]); if (em.v1 && E.v3Detect(em.rom)) linkInto(d, 'pairHintV3'); if (em.t18) { t18ItemsInto(d, 'main'); t18PicsInto(d); t18BridgeInto(d); } });
  } else if (has && !state.emuRemove) {
    const nm = (F.embedded() && F.feldApps(state.ctx.rom, F.embedded().stub).name0) || mainName(state.ctx.rom) || 'EMU';
    const b = [], c7 = F.emuType(state.ctx.rom) === 't7' && E.t7Cfg(state.ctx.rom), go = c7 && c7.romLen !== 0xC0000, cfg = romEmuCfg(state.ctx.rom), big = cfg && DLC_DEV.includes(cfg.dev);
    if (go) b.push([t('t7FigPick') + (c7.figs.length ? ' (' + c7.figs.length + ')' : ''), () => { state.figT = 'main'; $('fileT7Fig').click(); }]);
    if (big) b.push([t('dlcPick'), () => busy(async () => { if (await mainGuestLoad()) { state.dlcT = 'main'; $('fileDlc').click(); } })]);
    b.push(['★', null, t('progIsMain'), 'pstar on', true]);
    b.push(['×', () => { state.emu = null; state.emuRemove = true; renderEmu(); }, t('emuOff')]);
    row('main', nm, tagMain, 'main', b, (d) => {
      mainSet(d, cfg ? cfg.dev : F.emuType(state.ctx.rom));
      if (go) { figsInto(d, 'main'); t7GpInto(d, 'main'); linkInto(d, 'pairHintT7'); }
      if (cfg && IR_HINT[cfg.dev]) linkInto(d, IR_HINT[cfg.dev]);
      if (F.emuType(state.ctx.rom) === 't18') { t18ItemsInto(d, 'main'); t18PicsInto(d); t18BridgeInto(d); }
      if (big) { if (state.mainGuest) dlcInto(d, 'main'); else { const w = document.createElement('div'); w.className = 'hint'; w.textContent = t('dlcLoading'); d.appendChild(w); busy(async () => { await mainGuestLoad(); renderEmu(); }); } }
    });
  } else if (has && state.emuRemove) {
    const nm = (F.embedded() && F.feldApps(state.ctx.rom, F.embedded().stub).name0) || mainName(state.ctx.rom) || 'EMU';
    row('main', nm, t('progGone'), 'main gone', [['↺', undo, t('progUndo')]]);
  }
  // further programs (started from the menu): each with its own clock setting
  (state.extras || []).forEach((x, i) => {
    const b = [], go = !x.kind && E.t7Kind(x.rom) === 'go', big = x.kind === 'tama' && DLC_DEV.includes(x.dev);
    if (go) b.push([t('t7FigPick') + (x.figs && x.figs.length ? ' (' + x.figs.length + ')' : ''), () => { state.figT = i; $('fileT7Fig').click(); }]);
    if (big) b.push([t('dlcPick'), () => { state.dlcT = i; $('fileDlc').click(); }]);
    const xMain = lp && state.mainPick === x;
    if (lp) b.push(star(xMain, x));
    else { const ok = !dm && canMain(x); b.push(['☆', ok ? () => busy(() => makeMain(i)) : null, t(ok ? 'progMakeMain' : x.kind === 'gb' ? 'gbNoMain' : dm ? 'progNoMainDoom' : 'progNoMain'), 'pstar', !ok]); }
    b.push(['×', () => { if (state.mainPick === x) { state.mainPick = null; state.mainDirty = true; } state.extras.splice(i, 1); state.extrasDirty = true; state.t7saveX = null; state.dlcT = 'main'; state.figT = 'main'; state.progOpen = new Set(); renderEmu(); }, t('progDel')]);
    const det = ((x.kind === 'v1' && !E.v1Clock(x.rom)) || x.kind === 'gb') && !xMain ? null : (d) => {   // test37: the Game Boy has no clock
      if (xMain) autoL(d);
      if (!(x.kind === 'v1' && !E.v1Clock(x.rom)) && x.kind !== 'gb') checkInto(d, t('emuClock'), t('emuClockHint'), x.clock !== false, (v) => { x.clock = v; state.extrasDirty = true; renderEmu(); });
      if (x.kind === 'tama' || x.kind === 't18') { x.opt = x.opt || { vol: 0, dim: 0, off: 0 }; optInto(d, x.opt, () => { state.extrasDirty = true; renderEmu(); }); }
      if (x.kind === 'tama' && (x.dev === '4u' || x.dev === '4u-plain')) checkInto(d, t('emuNfc'), t('emuNfcHint'), x.nfc !== false, (v) => { x.nfc = v; state.extrasDirty = true; renderEmu(); });
      if (go) { figsInto(d, i); t7GpInto(d, i); linkInto(d, 'pairHintT7'); }
      if (big) dlcInto(d, i);
      if (x.kind === 'tama' && IR_HINT[x.dev]) linkInto(d, IR_HINT[x.dev]);
      if (x.kind === 'v1' && E.v1Name(x.rom).startsWith('DIGIMON')) linkInto(d, 'pairHintDigi');
      if (x.kind === 'v1' && E.v3Detect(x.rom)) linkInto(d, 'pairHintV3');
      if (x.kind === 't18') { t18ItemsInto(d, i); t18BridgeInto(d); }
    };
    row('x' + i, E.extraName(x), xMain ? tagMain : t('progMenu'), xMain ? 'main' : '', b, det);
  });
  if (!box.children.length) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = t('progNone'); box.appendChild(d); }
  else if ((state.extras || []).length && !menuWanted()) { const d = document.createElement('span'); d.className = 'hint'; d.textContent = t('appNeedMenu'); box.appendChild(d); }
}
/* 1.6.8: choose which program is the main one (A and C, or the first entry of the start device) */
/* does a program in the menu come from an older Studio? (vectors and code differ from the program in this one) */
function extrasOld(rom, apps) {
  try {
    const progs = { t7: E.t7Embedded(), tama: E.embedded() && E.embedded().app, t18: E.t18Embedded(), v1: E.v1Embedded(), gb: E.gbEmbedded() };
    for (const x of E.appsRead(rom, apps)) {
      const k = E.extraKind(x), img = k === 't18' ? null : progs[k]; if (!img) continue;   // T18Emu: its image is patched all over
      const at = x.off, same = (a, z) => { for (let i = a; i < z; i++) if (rom[at + i] !== img[i]) return false; return true; };
      if (!same(0, 0x140) || !same(0x800, Math.min(img.length, 0x10000))) return true;
    }
  } catch (e) { return false; }
  return false;
}
/* 1.6.8: choosing or dropping an emulator does not bring back a DOOM the user took out (emuRemove means "take the
 * program at 0x0A0000 out", which is DOOM when DOOM is built in) */
function emuRemoveClear() { if (!doomInstalled()) state.emuRemove = false; }
/* 1.6.9 test13: the Studio's menu program knows the main program out of its list (version 9) */
function mainV9() { const fe = F.embedded(); if (!fe) return false; const f = fe.feld; for (let i = 0; i + 8 <= f.length; i += 4) if ((f[i] | f[i + 1] << 8 | f[i + 2] << 16 | f[i + 3] << 24) >>> 0 === 0x444C4546) return (f[i + 4] | f[i + 5] << 8) >= 9; return false; }
function doomMain() { return !!state.doom || (doomInstalled() && !state.emuRemove && (!!state.launch || !state.emu)); }
function curMainX() {
  const em = state.emu;
  if (em) return emuX(em);
  return null;
}
function canMain(x) {
  if (x.kind === 'gb') return false;   // 1.6.9 test36: GBEmu only in the menu
  // outside a start device only T7Emu goes to the menu, so the old main program must be a T7Emu (or none)
  if (state.launch) return true;
  if (!a0Free()) return false;   // test25: no main program at 0x0A0000 in this edition
  const em = state.emu, ty = em ? (em.v1 ? 'v1' : em.t7 ? 't7' : em.t18 ? 't18' : 'tama') : (emuInstalled() && !state.emuRemove ? F.emuType(state.ctx.rom) : null);
  return !ty || ty === 't7' || ty === 'v1';
}
async function makeMain(i) {
  const x = state.extras[i]; if (!x) return;
  let old = curMainX();
  if (!old && emuInstalled() && !state.emuRemove) old = await installedX(state.ctx.rom);
  let em;
  if (x.kind === 't18') { let n = 0; for (let k = 0; k < 128; k++) if (!x.fw.subarray(k << 16, (k + 1) << 16).every((v) => v === 0xFF)) n++; em = { t18: true, dev: 't18', rom: x.fw, size: n << 16 }; }
  else if (x.kind === 'tama') { const pk = E.packGuest(x.guest, x.dev); em = { guest: x.guest, dev: x.dev, size: pk.blob.length, big: pk.big }; }
  else if (x.kind === 'v1') em = { v1: true, dev: 'v1', rom: x.rom, size: x.rom.length + 0x2000 };
  else { em = { t7: true, dev: 't7', rom: x.rom, figs: x.figs || [] }; em.size = t7Size(em); }
  state.extras.splice(i, 1);
  if (old) old.opt = state.emuOpt;
  state.emuOpt = x.opt || { vol: 0, dim: 0, off: 0 };
  // outside a start device only T7Emu and V1Emu go to the menu; another main program is replaced
  if (old && !state.launch && (old.kind === 'tama' || old.kind === 't18')) { toast(t('progReplaced', { n: E.extraName(x), o: E.extraName(old) })); old = null; }
  if (old) state.extras.unshift(old);
  state.emu = em; emuRemoveClear(); state.extrasDirty = true; state.t7saveX = null; state.progOpen = new Set(); state.dlcT = 'main'; state.figT = 'main'; state.mainGuest = null;
  if (!state.launch) state.doom = null;
  if (!x.fresh) toast(t('progMainNow', { n: E.extraName(x) }));
  renderDoom(); renderEmu();
}
/* 1.6.8: the main program (state.emu) in the form of a further program, its name; the installed one read from the ROM */
function emuX(em) { return !em ? null : em.v1 ? { kind: 'v1', rom: em.rom } : em.t18 ? { kind: 't18', fw: em.rom } : em.t7 ? { rom: em.rom, figs: em.figs } : { kind: 'tama', guest: em.guest, dev: em.dev }; }
function emuNm(em) { return em.v1 ? E.v1Name(em.rom) : em.t7 ? E.t7XName(em.rom) : em.t18 ? E.extraName({ kind: 't18' }) : E.tamaName(em.guest, em.dev); }
async function installedX(r0) {
  const ty = F.emuType(r0);
  if (ty === 't7') { const c7 = E.t7FromRom(r0); return c7 ? { rom: c7.rom, figs: c7.figs } : null; }
  if (ty === 't18') { const fw = E.t18FromRom(r0); return fw ? { kind: 't18', fw } : null; }
  if (ty === 'v1') { const r = E.v1FromRom(r0); return r ? { kind: 'v1', rom: r } : null; }
  const cfg = romEmuCfg(r0); return cfg ? { kind: 'tama', guest: await SV.emuGuestAsync((a, n) => r0.subarray(a, a + n), cfg), dev: cfg.dev } : null;
}
function hasOldSave() { return typeof state.dlcT === 'number' ? !!state.extras[state.dlcT] && !state.extras[state.dlcT].fresh : !!(state.emu && state.emu.kept) || !!state.mainGuest; }
$('btnApp').onclick = () => $('fileApp').click();
$('fileApp').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  await addExtra(new Uint8Array(await f.arrayBuffer()), f.name);
};
/* 1.6.8: trial build of the ROM as it would be saved now; KB missing for the programs, 0 if they fit */
let lastRoom = 0;
async function roomMissing() {
  lastRoom = 0; toast.mute = {};
  try { await composeRom(); } catch (e) { /* only the room counts here */ } finally { toast.mute = null; }
  return lastRoom;
}
async function addExtra(g, fname) {
  let x = null;
  if (E.gbDetect(g)) { if (E.gbColorOnly(g)) { toast(t('gbColor', { f: fname }), true); return; } x = { kind: 'gb', rom: g, keys: gbKeysNow().slice(), inv: !!state.gbInv }; }   // 1.6.9 test36: GBEmu
  else if (E.v1Detect(g)) x = { kind: 'v1', rom: g };   // 1.6.8: V1Emu (first Tamagotchis, Digital Monster)
  else if (E.t7Detect(g)) x = { rom: g };
  else if (E.t18Detect(g)) x = { kind: 't18', fw: g };
  else { try { const dev = E.emuDetect(g); E.packGuest(g, dev); x = { kind: 'tama', guest: g, dev }; } catch (err) { x = null; } }
  if (!x) {
    let main = false; try { main = !!E.t18Detect(g) || (E.emuDetect(g) && true); } catch (err) { main = false; }
    toast(t(main && !state.launch ? 'appBadMain' : state.launch ? 'appBadL' : 'appBad', { f: fname }), true); return;
  }
  state.extras = state.extras || [];
  if (state.extras.length >= E.APPX_MAX) { toast(t('appMax'), true); return; }
  { let m = curMainX(); if (!m && emuInstalled() && !state.emuRemove) { const r0 = state.ctx.rom, ty = F.emuType(r0); if (ty === 't7') { const c7 = E.t7FromRom(r0); if (c7) m = { rom: c7.rom }; } else if (ty === 't18') { const fw = E.t18FromRom(r0); if (fw) m = { kind: 't18', fw }; } else if (ty === 'v1') { const r = E.v1FromRom(r0); if (r) m = { kind: 'v1', rom: r }; } else if (x.kind === 'tama') { await mainGuestLoad(); if (state.mainGuest) m = { kind: 'tama', guest: state.mainGuest.g, dev: state.mainGuest.dev }; } } if ((m && sameFw(x, m)) || state.extras.some((y) => sameFw(x, y))) { toast(t('appTwice', { n: E.extraName(x) }), true); return; } }   // 1.6.8: each program once
  if (x.kind === 't18' && (state.extras.some((y) => y.kind === 't18') || (state.emu && state.emu.t18) || (!state.emu && F.emuType(state.ctx.rom) === 't18' && !state.emuRemove))) { toast(t('appT18One'), true); return; }
  x.fresh = true; state.extras.push(x); state.extrasDirty = true; state.t7saveX = null;
  // test25: no room at 0x0A0000 and no start device: a colour Tama or TG18 cannot go anywhere
  if (!state.launch && !a0Free() && (x.kind === 'tama' || x.kind === 't18')) { state.extras = state.extras.filter((y) => y !== x); toast(t('a0NoMain', { f: fname }), true); renderEmu(); return; }
  // outside a start device a colour Tama or TG18 only runs as the main program (A and C): it becomes the main one
  if (!state.launch && (x.kind === 'tama' || x.kind === 't18')) { busy(() => makeMain(state.extras.length - 1)); return; }
  // 1.6.8: does it still fit? A trial build right away, not only when writing
  const k = await busy(roomMissing);
  if (k) { state.extras = state.extras.filter((y) => y !== x); renderEmu(); toast(t('noRoomAdd', { n: E.extraName(x), k }), true); return; }
  renderEmu();
}
$('btnT7Fig').onclick = () => $('fileT7Fig').click();
$('fileT7Fig').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; if (!files.length) return;
  const st = figTarget(); if (!st) return;
  const xi = typeof state.figT === 'number';
  for (const f of files) {
    const raw = new Uint8Array(await f.arrayBuffer());
    if (!E.t7FigDetect(raw)) { toast(t('t7FigBad', { f: f.name }), true); continue; }
    const d = E.t7FigData(raw);
    if (st.figs.length >= 16) { toast(t('t7FigMax'), true); break; }
    if (st.figs.some((x) => E.t7FigName(x.name) === E.t7FigName(f.name))) { toast(t('t7FigDup', { f: E.t7FigName(f.name) }), true); continue; }
    if (!xi && E.t7Room(st.figs.concat([{ name: f.name, data: d }]), st.rom.length) < 0) { toast(t('t7FigFull', { f: E.t7FigName(f.name) }), true); continue; }
    st.figs = st.figs || []; st.figs.push({ name: f.name, data: d });
  }
  if (xi) { state.extrasDirty = true; state.t7saveX = null; renderEmu(); return; }
  st.size = t7Size(st); state.emu = st; emuRemoveClear(); if (!state.launch) state.doom = null;
  renderDoom(); renderEmu();
};
/* Download content of the 8 MB guest firmware: one box per store, every slot with its name */
function renderDlc() { $('dlcList').innerHTML = ''; }   // 1.6.8: the slots are in the program's row
$('btnDlc').onclick = () => $('fileDlc').click();
$('fileDlc').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; if (!files.length || !dlcTarget()) return;
  let nb0 = -1; try { const t0 = dlcTarget(); nb0 = E.packGuest(t0.guest, t0.dev).blocks; } catch (err) { nb0 = -1; }
  for (const f of files) {
    try {
      const tg = dlcTarget();
      const rec = E.dlcRecord(new Uint8Array(await f.arrayBuffer()), tg.dev);
      const r = E.dlcAdd(tg.guest, rec);
      tg.set(r.guest);
      toast(t('dlcOk', { n: E.dlcName(rec), k: t('dlcK' + r.store.kind), s: r.slot + 1 }));
    } catch (err) { toast(t('dlcBad', { f: f.name, e: em(err) }), true); }
  }
  // the first item in a still empty store needs a new block: the emulator's layout changes, its save starts anew
  try { const t1 = dlcTarget(); if (nb0 >= 0 && E.packGuest(t1.guest, t1.dev).blocks !== nb0 && hasOldSave()) toast(t('dlcNewArea'), true); } catch (err) { /* only a hint */ }
  renderEmu();
};
$('fileT18Item').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; const T = t18Target(state.itemT); if (!files.length || !T) return;
  const recs = [];
  for (const f of files) {
    try {
      const b = new Uint8Array(await f.arrayBuffer());
      if (f.name.toLowerCase().endsWith('.zip')) { const z = await C.zipRead(b, inflateRaw); for (const x of z) recs.push(...E.t18iScan(x.data)); }
      else recs.push(...E.t18iScan(b));
    } catch (err) { toast(t('dlcBad', { f: f.name, e: em(err) }), true); }
  }
  if (!recs.length) { toast(t('t18ItemsNone'), true); return; }
  let n = 0;
  for (const r of recs) {
    if (r.model && r.model !== 'tg18') toast(t('t18ItemsOther', { n: t18Nm(r), m: r.model.toUpperCase() }), true);
    try { const x = E.t18ItemAdd(T.fw, r, 1); n++; toast(t('t18ItemOk', { n: t18Nm(r), k: t('t18A_' + x.area) })); }
    catch (err) { toast(t('dlcBad', { f: t18Nm(r), e: err.message === 'full' ? t('t18ItemsFull') : em(err) }), true); }
  }
  if (n) T.set();
  renderEmu();
};
$('fileT18Pic').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = ''; if (!files.length) return;
  const ents = [];
  for (const f of files) {
    const b = new Uint8Array(await f.arrayBuffer());
    if (f.name.toLowerCase().endsWith('.zip')) { try { for (const x of await C.zipRead(b, inflateRaw)) ents.push({ name: x.name.split('/').pop(), data: x.data }); } catch (err) { toast(t('dlcBad', { f: f.name, e: em(err) }), true); } }
    else ents.push({ name: f.name, data: b });
  }
  const byImg = new Map();   // the Android set: erwachsen.json names every picture
  // English firmwares (Param block at 0xF800D864) take the pictures of the English ones first, the others the Japanese ones
  const fw0 = state.emu && state.emu.t18 ? state.emu.rom : null, pa = fw0 ? E.t18Addrs(fw0).param : (state.ctx.rom[0xA0140 + 428] | state.ctx.rom[0xA0140 + 429] << 8 | state.ctx.rom[0xA0140 + 430] << 16 | state.ctx.rom[0xA0140 + 431] << 24) >>> 0;
  const EN = new Set(['34525147dbe95eb512760981a07f5c30', '1a7d362f6f31b637e0a3b34ba6b6d110', '190a9d6801ed709f9a090a74f95039af']), wantEn = !pa || pa === 0xF800D864;
  const rank = new Map();
  for (const x of ents) if (/\.json$/i.test(x.name)) { try { const j = JSON.parse(new TextDecoder().decode(x.data)); for (const k in j) for (const a of j[k] || []) if (a.img) { byImg.set(a.img, a.id); rank.set(a.img, EN.has(k) === wantEn ? 0 : 1); } } catch (err) { /* not that file */ } }
  ents.sort((a, b) => (rank.has(b.name) ? rank.get(b.name) : 0) - (rank.has(a.name) ? rank.get(a.name) : 0));   // the wanted ones last: they win
  state.t18Pics = state.t18Pics || new Map(); let n = 0;
  for (const x of ents) {
    if (!/\.png$/i.test(x.name)) continue;
    const id = byImg.has(x.name) ? byImg.get(x.name) : E.t18AdultFind(x.name);
    if (id < 0 || !E.T18_ADULTS.some((a) => a[0] === id)) { toast(t('t18PicsUnknown', { f: x.name }), true); continue; }
    try { state.t18Pics.set(id, E.t18PicMake(await pngRgba(x.data))); n++; } catch (err) { toast(t('dlcBad', { f: x.name, e: em(err) }), true); }
  }
  if (n) { state.t18PicsDirty = true; toast(t('t18PicsOk', { n })); }
  renderEmu();
};
$('btnEmu').onclick = () => $('fileEmu').click();
$('fileEmu').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  const g = new Uint8Array(await f.arrayBuffer());
  // 1.6.8: one button for everything: the first program is the main one (A and C), every further one goes to the menu
  if (E.gbDetect(g)) { await addExtra(g, f.name); renderGb(); return; }   // 1.6.9 test36: Game Boy games go to the menu
  if (state.emu || (emuInstalled() && !state.emuRemove) || (state.extras || []).length || (E.v1Detect(g) && doomMain()) || !a0Free()) { await addExtra(g, f.name); return; }   // test25: no room at 0x0A0000: the menu   // the first program stays the main one
  if (E.v1Detect(g)) { state.emu = { v1: true, dev: 'v1', rom: g, size: g.length + 0x2000 }; extrasDedupe(null); emuRemoveClear(); if (!state.launch) state.doom = null; renderDoom(); }
  else if (E.t18Detect(g)) {
    let n = 0; for (let i = 0; i < 128; i++) if (!g.subarray(i << 16, (i + 1) << 16).every((v) => v === 0xFF)) n++;
    state.emu = { t18: true, dev: 't18', rom: g, size: n << 16 }; extrasDedupe(null); emuRemoveClear(); if (!state.launch) state.doom = null; renderDoom();
  } else if (E.t7Detect(g)) {
    const old = state.emu && state.emu.t7 ? state.emu : t7State();   // the figures stay
    const st = { t7: true, dev: 't7', rom: g, figs: old && E.t7Kind(g) === 'go' ? old.figs : [] };
    st.size = t7Size(st); state.emu = st; extrasDedupe(null); emuRemoveClear(); if (!state.launch) state.doom = null; renderDoom();
  } else
  try { const dev = E.emuDetect(g); const pk = E.packGuest(g, dev); state.emu = { guest: g, dev, size: pk.blob.length, big: pk.big }; extrasDedupe(null); emuRemoveClear(); if (!state.launch) state.doom = null; renderDoom(); }
  catch (err) { toast(t('emuBad', { e: em(err) }), true); }
  renderEmu();
};
$('emuAuto').onchange = (e) => { state.emuAuto = e.target.checked; renderEmu(); };
$('emuClock').onchange = (e) => { state.emuClock = e.target.checked; renderEmu(); };
$('emuNfc').onchange = (e) => { state.emuNfc = e.target.checked; renderEmu(); };
$('btnEmuOff').onclick = () => { state.emu = null; state.emuRemove = true; renderEmu(); };
$('btnEmuUndo').onclick = () => { state.emu = null; emuRemoveClear(); state.t7save = null; renderEmu(); renderDoom(); };

// ---------------------------------------------------------------- DOOM
function keySel(id, val, wheel) {
  const sel = $(id); sel.innerHTML = '';
  [[1, 'A'], [2, 'B'], [4, 'C']].concat(wheel ? [[8, t('kWheel')]] : []).forEach(([v, l]) => { const o = document.createElement('option'); o.value = v; o.textContent = l; sel.appendChild(o); });
  sel.value = String(val);
}
/* 1.6.9 test36: tab Game Boy. The games go into the menu (GBEmu, kind 'gb' in state.extras); one key layout for all. */
const GB_BTNS = [[0, 'gbNone'], [1, 'A'], [2, 'B'], [8, 'Start'], [4, 'Select'], [16, 'gbRight'], [32, 'gbLeft'], [64, 'gbUp'], [128, 'gbDown']];
const GB_INS = ['gbInA', 'gbInB', 'gbInC', 'gbInWp', 'gbInWu', 'gbInWd', 'gbInAC', 'gbInBC', 'gbInAB'];
function gbKeysNow() { if (!state.gbKeys) { const x = (state.extras || []).find((y) => y.kind === 'gb' && y.keys && y.keys.length === 9); state.gbKeys = x ? x.keys.slice() : E.GB_KEYS_DEFAULT.slice(); if (x) state.gbInv = !!x.inv; } return state.gbKeys; }
function gbKeysApply() { for (const x of state.extras || []) if (x.kind === 'gb') { x.keys = state.gbKeys.slice(); x.inv = !!state.gbInv; } state.extrasDirty = true; }
function renderGb() {
  if (!$('gbList')) return;
  const xs = (state.extras || []).map((x, i) => [x, i]).filter(([x]) => x.kind === 'gb');
  const box = $('gbList'); box.innerHTML = '';
  if (!xs.length) { const d = document.createElement('div'); d.className = 'hint'; d.textContent = t('gbNone2'); box.appendChild(d); }
  for (const [x, i] of xs) {
    const r = document.createElement('div'); r.className = 'dlcrow';
    const n = document.createElement('span'); n.className = 'itemname'; n.textContent = E.extraName(x) + '  (' + (x.rom.length >> 10) + ' KB)';
    const b = document.createElement('button'); b.className = 'btn small'; b.textContent = '×'; b.title = t('progDel');
    b.onclick = () => { state.extras.splice(i, 1); state.extrasDirty = true; renderEmu(); };
    r.append(n, b); box.appendChild(r);
  }
  if (xs.length && !menuWanted()) { const d = document.createElement('div'); d.className = 'hint'; d.textContent = t('appNeedMenu'); box.appendChild(d); }
  const k = gbKeysNow(), g = $('gbKeyGrid'); g.innerHTML = '';
  GB_INS.forEach((nm, i) => {
    const l = document.createElement('label'); const sp = document.createElement('span'); sp.textContent = t(nm);
    const sel = document.createElement('select');
    for (const [v, tn] of GB_BTNS) { const o = document.createElement('option'); o.value = String(v); o.textContent = tn.startsWith('gb') ? t(tn) : tn; sel.appendChild(o); }
    sel.value = String(GB_BTNS.some(([v]) => v === k[i]) ? k[i] : 0);
    sel.onchange = () => { state.gbKeys[i] = +sel.value; gbKeysApply(); renderGb(); };
    l.append(sp, sel); g.appendChild(l);
  });
  $('gbInv').checked = !!state.gbInv;
  const used = new Set(k.filter((v) => v)); const miss = [[1, 'A'], [2, 'B'], [8, 'Start']].filter(([v]) => !used.has(v)).map(([, n]) => n);
  $('gbKeysStat').textContent = miss.length ? t('gbKeysMiss', { n: miss.join(', ') }) : '';
}
$('btnGb').onclick = () => { if (!state.ctx) { toast(t('gbNeedRom'), true); return; } $('fileGb').click(); };
$('fileGb').onchange = async (e) => {
  const files = [...e.target.files]; e.target.value = '';
  for (const f of files) {
    const g = new Uint8Array(await f.arrayBuffer());
    if (!E.gbDetect(g)) { toast(t('gbBad', { f: f.name }), true); continue; }
    await addExtra(g, f.name);
  }
  renderGb();
};
$('gbInv').onchange = () => { gbKeysNow(); state.gbInv = $('gbInv').checked; gbKeysApply(); };
$('btnGbKeysDef').onclick = () => { state.gbKeys = E.GB_KEYS_DEFAULT.slice(); state.gbInv = false; gbKeysApply(); renderGb(); };
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
  else if (!state.launch && !d && (state.emu || (emuInstalled() && !state.emuRemove))) s += '\n' + t('doomVsEmu');
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
$('btnDoomOff').onclick = () => { state.doom = null; if (!state.launch) state.emu = null; state.emuRemove = true; renderEmu(); renderDoom(); };
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
      state.doom = { wad: w, eps, fit, ok }; if (!state.launch) state.emu = null; state.emuRemove = false; renderEmu();
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
// installed six fields with the options chosen now?
const sixSame = (si) => !!si && si.planet === (state.sixPlanet !== false) && si.fieldMenu === (state.sixMenu !== false);
function renderFeld() {
  if (!state.ctx) return;
  const fe = F.embedded(), rom = state.ctx.rom, ed = F.edition(rom), can = F.canFields(rom);
  const box = $('feldOn'), bag = $('bagOn'), tm = $('tamaOn'), la = !!state.launch;
  $('launchOn').checked = la; $('launchOn').disabled = !fe || !ed || (state.launchInit && C.isLauncher(state.ctx)) || !!S6.sixInfo(rom); $('launchOn').parentNode.title = S6.sixInfo(rom) ? t('launchSix') : '';
  box.checked = !!state.feld && can && !la; box.disabled = !fe || !can || la;
  bag.checked = !!state.bag && !la; bag.disabled = !fe || !ed || la;
  tm.checked = !!state.tama && !la; tm.disabled = !fe || !ed || la;
  let s;
  // six fields (Tropical/Glacier): row visible there only
  const si = S6.sixInfo(rom), sixIn = !!si, sixOld = !!si && si.version < S6.VERSION, sixCan = !!S6.kindOf(rom), jade = S6.kindOf(rom) === 'jade';
  $('sixRow').hidden = !sixCan || la;
  for (const [id, k] of [['sixOnTxt', jade ? 'sixOnJ' : 'sixOn'], ['sixHintTxt', jade ? 'sixHintJ' : 'sixHint']]) { $(id).dataset.t = k; $(id).textContent = t(k); }
  $('sixOn').checked = (sixIn && !state.sixOff) || !!state.six || !!state.sixWant; $('sixOn').disabled = !fe || !S6.embedded();
  $('btnSix').hidden = jade || (sixIn && !sixOld && sixSame(si));
  $('sixPlanetRow').hidden = !sixCan || la || !((sixIn && !state.sixOff) || state.six || state.sixWant);
  $('sixPlanet').checked = state.sixPlanet !== false; $('sixPlanet').disabled = !fe || !S6.embedded();
  $('sixMenu').checked = state.sixMenu !== false; $('sixMenu').disabled = !fe || !S6.embedded();
  if (!fe) s = t('feldMissing');
  else if (!ed) s = t('feldUnknown');
  else if (la) s = t('launchUse') + (state.launchInit && C.isLauncher(state.ctx) ? '\n' + t('launchOld') : '');
  else {
    s = t('feldCur', { f: F.fieldNow(rom) });
    if (can) {
      s += ' ' + t('feldList', { s: F.fieldNames(rom).slice(0, sixIn && state.sixOff ? 3 : 99).concat(!sixIn && (state.six || (jade && state.sixWant)) ? (jade ? ['Sky'] : ['Land', 'Water', 'Sky']) : []).join(', ') });
      const ff = F.feldFind(rom, fe.stub), sm = ff && ff.version >= 6 && (ff.flags & F.F_SAVES);
      const sl = F.feldSlots(rom, fe.stub).map(x => sm ? t('saveSlot', { n: x.index + 1 }) : F.NAMES[x.field]);
      if (sl.length) s += ' ' + t('feldStored', { s: sl.join(', ') });
    } else s += '\n' + t('feldNo');
    if (sixCan && sixIn && state.sixOff) s += '\n' + t(jade ? 'sixOffJ' : 'sixOff');
    else if (sixCan) {
      if (jade) { if (sixOld) s += '\n' + t('sixOldJ'); else if (sixIn) s += '\n' + t('sixInJ'); else if (!S6.sixCan(rom)) s += '\n' + t('sixNoFw'); }
      else if (sixOld && state.six) s += '\n' + t('sixOk', { n: state.sixName });
      else if (sixOld) s += '\n' + t('sixOld');
      else if (sixIn && !sixSame(si) && !state.six) s += '\n' + t('sixPlanetNeed');
      else if (sixIn) s += '\n' + t('sixIn');
      else if (!S6.sixCan(rom)) s += '\n' + t('sixNoFw');
      else if (state.sixWant && !state.six) s += '\n' + t('sixNeed');
      else if (state.six) s += '\n' + t('sixOk', { n: state.sixName });
    }
    if (state.feld || state.bag || state.tama) s += '\n' + t('feldUse');
  }
  $('feldStat').textContent = s;
  renderPlan();
}
$('feldOn').onchange = (e) => { state.feld = e.target.checked; renderFeld(); };
$('launchOn').onchange = (e) => busy(async () => {
  state.launch = e.target.checked;
  if (state.launch) { launchMapGet(); state.feld = state.bag = state.tama = false; state.sixWant = false; state.six = null; }
  renderInfo(); renderFeld(); renderEmu(); renderNames();
});
$('sixOn').onchange = (e) => {
  state.sixOff = !e.target.checked && !!S6.sixInfo(state.ctx.rom);   // 1.6.8: take the extra fields out again
  state.sixWant = e.target.checked;
  if (!e.target.checked) state.six = null;
  else { state.feld = true; if (!state.six && S6.sixNeedsSource(state.ctx.rom)) $('fileSix').click(); }
  renderFeld(); renderNames();
};
$('btnSix').onclick = () => $('fileSix').click();
$('sixPlanet').onchange = (e) => { state.sixPlanet = e.target.checked; renderFeld(); };
$('sixMenu').onchange = (e) => { state.sixMenu = e.target.checked; renderFeld(); };
$('fileSix').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  const buf = new Uint8Array(await f.arrayBuffer());
  if (!S6.sixSource(buf)) { toast(t('sixBad', { n: f.name }), true); renderFeld(); return; }
  state.six = buf; state.sixName = f.name; state.sixWant = true; state.feld = true;
  toast(t('sixOk', { n: f.name }));
  renderFeld(); renderNames();
};
$('bagOn').onchange = (e) => { state.bag = e.target.checked; renderFeld(); };
$('tamaOn').onchange = (e) => { state.tama = e.target.checked; renderFeld(); };

/* Build the new ROM from the loaded one and everything chosen in the tabs.
   Returns { rom, name, res } or null (the reason is shown). same: nothing chosen. */
async function composeRom() {
  const keysDirty = !state.doom && doomInstalled() && state.doomKeys && D.doomKeysGet(state.ctx.rom) && JSON.stringify(state.doomKeys) !== JSON.stringify(D.doomKeysGet(state.ctx.rom));
  const si0 = S6.sixInfo(state.ctx.rom), sixUp = !!si0 && (si0.version < S6.VERSION || !sixSame(si0)) && !S6.sixNeedsSource(state.ctx.rom);
  const emuUp = emuUpPending();
  if (!state.changed.size && !state.t7save && !state.emu && !state.emuRemove && !state.doom && !keysDirty && !emuUp && !emuAutoDirty() && !state.six && !sixUp && !(state.sixWant && !S6.sixInfo(state.ctx.rom)) && state.feld === state.feldInit && state.bag === state.bagInit && state.tama === state.tamaInit && state.saves === state.savesInit && state.launch === state.launchInit && !state.feldStale && !state.extrasDirty && !state.mainDirty && !state.extrasUp && !state.t7saveX && !state.saveIn && !state.sixOff && !state.petDirty && !state.t18Dirty && !state.t18PicsDirty && !(state.texts && state.texts.size) && !(state.eyeShift && state.eyeShift.size) && !(state.mouthShift && state.mouthShift.size) && !(state.poseAdj && state.poseAdj.size)) return { rom: state.ctx.rom, name: state.name, res: null, same: true };
  const fe = F.embedded(); let feldSaved = fe ? F.feldSlots(state.ctx.rom, fe.stub) : [];
  // start device with DOOM (1.6.8): DOOM is the program at 0x0A0000, the emulator of the Emulator tab goes along as a further program
  const doomL = !!state.launch && (!!state.doom || (doomInstalled() && !state.emuRemove));
  let mainX = null;
  if (doomL && state.emu) mainX = emuX(state.emu);
  else if (doomL && state.doom && emuInstalled()) mainX = await installedX(state.ctx.rom);
  // 1.6.8: an extra that is the same firmware as the main emulator only once (a start device read back shows its
  // emulator as a further program; choosing it again in the Emulator tab must not put it in twice)
  if (mainX) { mainX.clock = state.emuClock !== false; mainX.nfc = state.emuNfc !== false; mainX.opt = state.emuOpt; }
  extrasDedupe(mainX);
  const extrasL = mainX ? [mainX].concat(state.extras || []) : (state.extras || []);
  if (state.emu && state.doom && !doomL) { toast(t('doomBoth'), true); return null; }
  if (state.doom && state.doomKeys && D.doomKeysCheck(state.doomKeys)) { toast(em(D.doomKeysCheck(state.doomKeys)), true); return null; }
  let rom = state.ctx.rom, res = null;
  const fresh = [];   // 1.6.8: ranges taken from the image even with "keep saves" (imported saves)
  if (state.launch && !FL.embedded()) { toast(t('emuMissing'), true); return null; }
  if (state.launch && !(state.emu || state.doom || (state.extras && state.extras.length) || (appInstalled() && !state.emuRemove))) { toast(t('launchNeed'), true); return null; }
  const lmap = state.launch ? launchMapGet() : null;
  if (state.changed.size || (lmap && lmap.size) || (state.eyeShift && state.eyeShift.size) || (state.mouthShift && state.mouthShift.size) || (state.poseAdj && state.poseAdj.size)) {
    const all = lmap ? new Map([...lmap, ...state.changed]) : state.changed;
    try { res = C.buildRom(state.ctx, all, currentDataPack()); rom = res.rom; }
    catch (e) { toast(t('tooBig', { e: em(e) }), true); return null; }
  }
  // 1.6.8: names of fields, Tamas and items (text tables)
  if (state.texts && state.texts.size) { try { rom = C.textApply(rom, state.texts); } catch (e) { toast(t('tooBig', { e: em(e) }), true); return null; } }
  if (fe) rom = F.feldStrip(rom, { stub: fe.stub, oldStub: E.embedded() && E.embedded().stub }, E);
  // Emulator instead of installed DOOM: remove DOOM completely first (program part, WAD), then install the emulator
  if (state.emu && doomInstalled() && !doomL) { try { rom = E.emuRemove(rom); } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; } }
  if ((state.emu && !doomL) || state.emuRemove) {
    const emb = E.embedded();
    try {
      if (state.emu && doomL) { /* goes along as a further program */ }
      else if (state.emu && state.emu.v1) { if (!emb || !E.v1Embedded()) throw new Error(t('emuMissing')); rom = E.v1Patch(rom, state.emu.rom, E.v1Embedded(), emb.stub, { lang, clock: state.emuClock !== false }); }
      else if (state.emu && state.emu.t7) { if (!emb || !E.t7Embedded()) throw new Error(t('emuMissing')); rom = E.t7Patch(rom, state.emu.rom, state.emu.figs, E.t7Embedded(), emb.stub, { lang }); }
      else if (state.emu && state.emu.t18) { if (!emb || !E.t18Embedded()) throw new Error(t('emuMissing')); rom = E.t18Patch(rom, state.emu.rom, E.t18Embedded(), emb.stub, { lang, launch: !!state.launch }).rom; }
      else if (state.emu) { if (!emb) throw new Error(t('emuMissing')); rom = E.emuPatch(rom, state.emu.guest, emb.app, emb.stub, { lang }).rom; }
      else rom = E.emuRemove(rom);
    } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; }
  }
  if (state.t7save && F.emuType(rom) === 't7' && !doomL) rom = SV.t7SaveIn(rom, state.t7save);
  if (state.saveIn && state.saveIn.key === 'm' && !doomL) {
    if (state.saveIn.kind === 'v1' && F.emuType(rom) === 'v1') { rom = new Uint8Array(rom); rom.set(state.saveIn.data, 0xC0000); fresh.push([0xC0000, 0xC2000]); }
    if (state.saveIn.kind === 'tama' && F.emuType(rom) === 'tama') { const g0 = rd32(rom, 0xA0148), se = rd32(rom, 0xA0144) >= 3 ? rd32(rom, 0xA0140 + 152) : g0 + rd32(rom, 0xA014C); fresh.push([0xC0000, 0x110000], [g0, se]); }
  }
  // newer emulator program: only the program changes, the Tamagotchi's save stays
  if (emuUp && F.appKind(rom) === 2) { const ty = F.emuType(rom), u = ty === 'v1' ? E.v1Upgrade(rom, E.v1Embedded()) : ty === 't7' ? E.t7Upgrade(rom, E.t7Embedded()) : ty === 't18' ? E.t18Upgrade(rom, E.t18Embedded(), E.embedded() && E.embedded().stub) : E.emuUpgrade(rom, E.embedded().app); if (u) rom = u; }
  if (state.t18Dirty && state.t18Main && !state.emu && !state.emuRemove && F.emuType(rom) === 't18') { try { rom = E.t18RomWriteGuest(rom, state.t18Main); t18Fresh(rom, 0xA0000, fresh); } catch (e) { toast(t('emuFail', { e: em(e) }), true); return null; } }   // test47: items
  if (F.emuType(rom) === 't18' && state.t18Pics && (state.t18Pics.size || state.t18PicsDirty)) rom = E.t18PicsWrite(rom, state.t18Pics);   // test47: grow-up pictures
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
  // 1.6.8: extra fields out again (several saves with the edition's own fields only)
  if (state.sixOff && S6.sixInfo(rom)) {
    const base = F.fieldNames(S6.sixStrip(state.ctx.rom)), now = F.fieldNow(state.ctx.rom);
    if (now && base.length && !base.includes(now)) { toast(t('sixOffField', { f: now, s: base.join(', ') }), true); return null; }
    if (feldSaved.length && !confirm(t('sixOffSlots'))) return null;
    rom = S6.sixStrip(rom); feldSaved = [];
  }
  const needSrc = S6.sixNeedsSource(rom);
  if (state.sixWant && needSrc && !state.six && !S6.sixInfo(rom)) { toast(t('sixNeed'), true); return null; }
  const sixHave = S6.sixInfo(rom);
  if (sixHave && needSrc && !state.six && sixHave.version >= S6.VERSION && !sixSame(sixHave)) { toast(t('sixPlanetNeed'), true); return null; }
  if ((state.six || (state.sixWant && !needSrc) || (!needSrc && sixHave)) && !(sixHave && sixHave.version >= S6.VERSION && sixSame(sixHave))) {
    if (state.doom || doomInstalled() || state.emu) { toast(t('sixFail', { e: t('sixApp') }), true); return null; }
    if (state.extras && state.extras.length) { toast(t('appSix'), true); return null; }
    try { rom = S6.sixApply(rom, state.six, S6.embedded(), { planet: state.sixPlanet !== false, fieldMenu: state.sixMenu !== false }); }
    catch (e) { toast(t('sixFail', { e: em(e) }), true); return null; }
  }
  if (S6.sixInfo(rom)) rom = S6.sixFix(rom);   // the archive may have moved: rain pictures point to 0xF00000
  // more emulators (1.6.0): the old blocks go, the list is placed again from the top of the emulator area
  let apps = [], appsLow = 0;
  if (fe) { const old = E.appsRead(state.ctx.rom, F.feldApps(state.ctx.rom, fe.stub).apps); if (old.length) rom = E.appsClear(rom, old.filter((o) => !extrasL.some((x) => x.kind === 't18' && x.keepOff === o.off))); }   // test2: an installed T18Emu stays
  let dl = 0;
  if (extrasL.length || state.launch) {
    if (!fe || !menuWanted()) { toast(t('appNeedMenu'), true); return null; }
    if (!E.t7Embedded()) { toast(t('emuMissing'), true); return null; }
    const progs = { t7: E.t7Embedded(), tama: E.embedded() && E.embedded().app, t18: E.t18Embedded(), v1: E.v1Embedded() };
    try { const r = E.appsPlace(rom, extrasL, progs, { lang, clock: state.emuClock !== false, nfc: state.emuNfc !== false, launch: !!state.launch, keep: F.appKind(rom) === 1 ? [[0x830000, 0xD48000]] : [], dlApp: state.launch ? FL.embedded() : null }); rom = r.rom; apps = r.list; appsLow = r.low; dl = r.dl;
      for (const x of extrasL) if (x.kind === 't18' && x.items && x.keepOff !== undefined) t18Fresh(rom, x.keepOff, fresh);   // test3
      if (state.t7saveX && apps[state.t7saveX.i + (mainX ? 1 : 0)]) rom = SV.t7SaveIn(rom, state.t7saveX.save, apps[state.t7saveX.i + (mainX ? 1 : 0)].off + 0x10000);
      if (state.saveIn && typeof state.saveIn.key === 'number' && apps[state.saveIn.key + (mainX ? 1 : 0)]) {
        const at = apps[state.saveIn.key + (mainX ? 1 : 0)].off, cx = E.extraCfg(rom, at);
        if (state.saveIn.kind === 'v1' && cx) { rom = new Uint8Array(rom); rom.set(state.saveIn.data, cx.saveOff); fresh.push([cx.saveOff, cx.saveOff + 0x2000]); }
        if (state.saveIn.kind === 'tama' && cx) fresh.push([cx.saveOff, cx.saveOff + 0x50000], [cx.guestOff, cx.spareEnd]);   // the imported pet: no run state of the device
      } }
    catch (e) { lastRoom = e && e.room ? e.missing : 0; toast(e && e.room ? t('noRoomKB', { k: e.missing }) : t('appFail', { e: em(e) }), true); return null; }
  }
  // 1.6.8: always the firmware's name (TAMA-GO, FRIENDS, ID L, 4U+ ...), not the emulator's
  // 1.6.9 test13: the chosen main program's place in the list (0 = the program at 0x0A0000)
  const mainSrcOf = () => { if (!state.launch || !state.mainPick || !doomL) return 0; const k = state.mainPick === 'emu' ? (mainX ? 0 : -1) : extrasL.indexOf(state.mainPick); return k >= 0 && apps[k] ? apps[k].src : 0; };
  const name0 = F.appKind(rom) === 1 ? 'DOOM' : state.emu && state.emu.guest ? E.tamaName(state.emu.guest, state.emu.dev) : (!state.emu && !state.emuRemove && fe && F.appKind(state.ctx.rom) === F.appKind(rom) && F.feldApps(state.ctx.rom, fe.stub).name0) || mainName(rom);
  if (fe && menuWanted()) {
    try { rom = F.feldApply(rom, { fields: state.feld, bag: state.bag, tama: state.tama, saves: state.saves, launch: !!state.launch, apps, name0, appsLow, dl, mainSrc: mainSrcOf(), auto: !!state.emuAuto, fieldNames: fieldTexts(rom).map((x) => x.name) }, feldSaved, { feld: fe.feld, stub: fe.stub }, E); }
    catch (e) { toast(t('feldFail', { e: em(e) }), true); return null; }
  }
  // autostart of the emulator (the stubs above were written with it off), clock switch (1.5.1)
  if (F.appKind(rom) === 2) rom = E.emuOptSet(E.emuNfcSet(E.emuClockSet(rom, state.emuClock !== false), state.emuNfc !== false), state.emuOpt);
  if (F.appKind(rom) === 2 || (F.appKind(rom) === 1 && !state.launch)) {   // 1.6.8: DOOM as the main program can start right away too
    let a = E.autoSet(rom, stubList(rom), !!state.emuAuto);
    const fe2 = F.embedded();
    // stub of an older Studio (no switch): put in the current one
    if (!a && state.emuAuto && E.embedded() && !(fe2 && F.feldFind(rom, fe2.stub))) {
      try { rom = E.installApp(rom, rom.slice(0xA0000, 0xB0000), E.embedded().stub); a = E.autoSet(rom, stubList(rom), true); } catch (e) { a = null; }
    }
    if (a) rom = a;
  }
  const name = baseName() + (state.changed.size ? '_paramix' : '') + (state.emu ? (state.emu.v1 ? '_v1' : state.emu.t7 ? '_t7' : state.emu.t18 ? '_t18' : '_emu') : '') + (state.doom ? '_doom_E' + $('doomEp').value : '') + (S6.sixInfo(rom) ? (S6.kindOf(rom) === 'jade' ? '_4fields' : '_6fields') : state.feld ? '_fields' : '') + (state.bag ? '_items' : '') + (state.launch ? '_start' : '') + '.bin';
  return { rom, name, res, fresh };
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
  uartWarnRender();
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
/* 1.6.9: simulated playdate partner for the Paradise (src/visit.js). Same link key and serial transport as the
 * flash app loader (src/flash.js). */
const VS = { io: null, v: null };
const VIS_ED = [['land', 'Land'], ['water', 'Water'], ['sky', 'Sky'], ['jade', 'Jade Forest'], ['tropics', 'Tropics'], ['glacier', 'Glacier']];
function visKey() { return window.ParamixVisit.parseKey(FL.linkKey); }
function visStat(m) { $('visStat').textContent = m; }
function visEdName() { const e = $('visEd').value; const x = VIS_ED.find((a) => a[0] === e); return x ? x[1] : e; }
function visRender() {
  $('btnVis').textContent = t(VS.io ? 'visStop' : 'visStart');
  $('btnVis').disabled = !navigator.serial;
}
{
  const sel = $('visEd');
  for (const [v, n] of VIS_ED) { const o = document.createElement('option'); o.value = v; o.textContent = n; sel.appendChild(o); }
  try { const e = localStorage.getItem('pmxVisitEd'); if (e) sel.value = e; } catch (e) {}
  if (!sel.value) sel.value = 'water';
  sel.onchange = () => { try { localStorage.setItem('pmxVisitEd', sel.value); } catch (e) {} if (VS.io && (!VS.v || VS.v.state.phase === 'wait' || VS.v.state.phase === 'done' || VS.v.state.phase === 'error')) visArm(); };
  visRender();
}
function visArm() {
  if (VS.v) VS.v.stop();
  const ed = visEdName();
  VS.v = window.ParamixVisit.createVisitor({
    edition: $('visEd').value, key: visKey(),
    send: (b) => VS.io.write(b),
    log: (e, i) => {
      if (e === 'start') visStat(t('visRun'));
      else if (e === 'sent') visStat(t('visPlay'));
      else if (e === 'done') { visStat(t('visDone', { ed })); setTimeout(() => { if (VS.io) visArm(); visStat(t('visDone', { ed })); }, 500); }
      else if (e === 'error') { visStat(i.text === 'key' ? t('visBadKey') : t('visErr', { e: i.text })); if (i.text !== 'key') setTimeout(() => { if (VS.io) visArm(); }, 1000); }
    },
  });
  visStat(t('visWait', { ed }));
}
async function visClose() {
  const io = VS.io; VS.io = null;
  if (VS.v) VS.v.stop(); VS.v = null;
  try { if (io) await io.close(); } catch (e) {}
  visRender();
}
$('btnVis').onclick = async () => {
  if (VS.io) { await visClose(); visStat(''); return; }
  if (!navigator.serial) { visStat(t('devNoSerial')); return; }
  if (dv.io) { visStat(t('uartBusy')); return; }
  let port;
  try { port = await navigator.serial.requestPort(); } catch (e) { return; }
  try { VS.io = await FL.serialOpen(port); } catch (e) { visStat(t('visErr', { e: e.message })); return; }
  VS.io.onData = (d) => { if (VS.v) VS.v.feed(d); };
  visRender(); visArm();
};
$('btnConn').onclick = async () => {
  if (VS.io) await visClose();
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
  let emuSame = false;
  if (keep) {
    const EMU = { 0x43584D50: 'TamaEmu', 0x37584D50: 'T7Emu', 0x38584D50: 'T18Emu', 0x31584D50: 'V1Emu' };
    let dm = 0;
    try { const p = await dv.d.read(0xA0000, 0x1000); dm = (p[0x140] | p[0x141] << 8 | p[0x142] << 16 | p[0x143] << 24) >>> 0; } catch (e) { dm = 0; }
    const im = (c.rom[0xA0140] | c.rom[0xA0141] << 8 | c.rom[0xA0142] << 16 | c.rom[0xA0143] << 24) >>> 0;
    // 1.6.8: the same firmware chosen again (same layout and data as on the device) keeps the save
    if (EMU[dm] && dm === im && state.emu) { try { emuSame = await emuSameDev(dv.d, c.rom, dm); } catch (e) { emuSame = false; } }
    // 1.6.8: a T7Emu or V1Emu that goes from the main program to the menu takes its save along (keepPlan): no question
    let moved = false;
    if (EMU[dm] && dm !== im && (dm === 0x37584D50 || dm === 0x31584D50)) {
      try {
        const p = await dv.d.read(0xA0000, 0x1000), u = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
        const ro = u(p, 0x148), rl = u(p, 0x14C), dc = await dv.d.crc(ro, rl);
        const fe0 = F.embedded();
        if (fe0) for (const x of E.appsRead(c.rom, F.feldApps(c.rom, fe0.stub).apps)) { const cx = E.extraCfg(c.rom, x.off); if (cx && cx.romLen === rl && FL.crc32(c.rom, cx.romOff, cx.romLen) === dc) moved = true; }
      } catch (e) { moved = false; }
    }
    if (EMU[dm] && !moved && (dm !== im || (state.emu && !emuSame))) {
      const n = EMU[im] ? (dm === im ? t('devEmuNew', { o: EMU[im] }) : EMU[im]) : t('devEmuNone');
      if (!confirm(t('devEmuLost', { o: EMU[dm], n }))) return;
    }
  }
  // test4: items or Tamapoints of T18Emu: its save on the device is replaced by the one in the ROM, "keep saves" or not
  if (keep && (state.t18Dirty || (state.extras || []).some((x) => x.kind === 't18' && x.items)) && !confirm(t('t18KeepAsk'))) return;
  devTask(async () => {
    const fe = F.embedded();
    const plan = keep ? FL.keepPlan(F, E, fe && { stub: fe.stub }, { emuChanged: !!state.emu && !emuSame, t7Save: !!state.t7save, t7SaveX: state.t7saveX ? state.t7saveX.i : -1, mine: studioSecs(state.ctx.rom, c.rom), fresh: c.fresh || [] }) : null;
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
function mainName(r) {
  const ty = F.emuType(r);
  if (ty === 't7') return E.t7XName(E.t7FromRom(r).rom);
  if (ty === 't18') return E.extraName({ kind: 't18' });
  if (ty === 'v1') { const v = E.v1FromRom(r); return v ? E.v1Name(v) : 'V1'; }
  const c = romEmuCfg(r); if (c) return E.extraName({ kind: 'tama', dev: c.dev });
  return '';
}
/* 1.6.8: an extra that is the same firmware as the main emulator only once */
function sameFw(a, b) {
  // 1.6.8: colour Tamas by their code blocks (the rest of the flash holds the pet's own writes)
  if (E.extraKind(a) === 'tama' && E.extraKind(b) === 'tama') {
    if (a.dev !== b.dev) return false;
    for (const k of [0, 1, 0x40, 0x41]) { const o = k << 16; if (o + 0x10000 > a.guest.length || o + 0x10000 > b.guest.length) continue; for (let i = o; i < o + 0x10000; i++) if (a.guest[i] !== b.guest[i]) return false; }
    return true;
  }
const ka = E.extraKind(a), kb = E.extraKind(b); if (ka !== kb) return false; const x = ka === 'tama' ? [a.guest, b.guest] : ka === 't18' ? [a.fw, b.fw] : [a.rom, b.rom]; if (!x[0] || !x[1]) return false; const [p0, p1] = x[0].length <= x[1].length ? x : [x[1], x[0]]; for (let i = 0; i < p0.length; i += 4093) if (p0[i] !== p1[i]) return false; for (let i = 0; i < p0.length; i++) if (p0[i] !== p1[i]) return false; for (let i = p0.length; i < p1.length; i++) if (p1[i] !== 0xFF) return false; return true;
}
function extrasDedupe(mainX) {
  const mainE = mainX || emuX(state.emu);
  if (mainE && state.extras && state.extras.some((x) => sameFw(x, mainE))) { state.extras = state.extras.filter((x) => !sameFw(x, mainE)); state.extrasDirty = true; }
  // the same firmware twice in the menu: once is enough (it would share nothing, only take room)
  const xs = state.extras || [], keep = xs.filter((x, i) => !xs.slice(0, i).some((y) => sameFw(x, y)));
  if (keep.length !== xs.length) { state.extras = keep; state.extrasDirty = true; }
}
/* 1.6.8: does the device hold the same emulator firmware (layout and data) as the image? */
/* 1.6.8: the sectors the Studio changed against the loaded ROM (download items in an emulator's firmware) */
const rd32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
function studioSecs(a, b) {
  const out = new Set(), n = Math.max(a.length, b.length);
  for (let s = 0; s < n; s += 0x1000) {
    if (s + 0x1000 > a.length || s + 0x1000 > b.length) { out.add(s); continue; }
    for (let i = s; i < s + 0x1000; i++) if (a[i] !== b[i]) { out.add(s); break; }
  }
  return out;
}
async function emuSameDev(d, img, m) {
  const p = await d.read(0xA0000, 0x1000), u = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  const eq = (a, z) => { for (let i = a; i < z; i++) if (p[i] !== img[0xA0000 + i]) return false; return true; };
  const crcEq = async (a, n) => n > 0 && a + n <= img.length && (await d.crc(a, n)) === FL.crc32(img, a, n);
  // TamaEmu: layout and block map, and the code blocks at the reset vector (the guest writes into other blocks
  // of its flash, those are kept from the device anyway)
  if (m === 0x43584D50) {
    if (!eq(0x148, 0x140 + 156)) return false;
    const rv = u(img, 0xA0140 + 20) === 0 ? 0 : 0x40, g = u(img, 0xA0148);
    for (const b of [rv, rv + 1]) { const k = img[0xA0140 + 24 + b]; if (k !== 0xFF && !(await crcEq(g + k * 0x10000, 0x10000))) return false; }
    return true;
  }
  if (m === 0x37584D50) return eq(0x148, 0x140 + 24 + 16 * 24) && crcEq(u(img, 0xA0148), u(img, 0xA014C));
  if (m === 0x31584D50) return eq(0x148, 0x154) && crcEq(u(img, 0xA0148), u(img, 0xA014C));   // V1Emu: ROM and its place  // T7Emu: ROM, figures
  if (m === 0x38584D50) {                                                                                   // T18Emu: block map and blocks
    if (!eq(0x14C, 0x140 + 400)) return false;
    const b0 = img[0xA0150] | img[0xA0151] << 8;   // the firmware's first block (code; it writes its save elsewhere)
    return b0 === 0xFFFF || crcEq(b0 << 16, 0x10000);
  }
  return false;
}
function romEmuCfg(rom) { return F.appKind(rom) === 2 && F.emuType(rom) === 'tama' ? SV.emuCfg(rom.subarray(0xA0000, 0xA1000)) : null; }
function romT7(rom) { return F.emuType(rom) === 't7'; }
/* Further T7Emus of the loaded ROM (1.6.0): [{ name, saveOff }] */
function romExtras(rom) { const fe = F.embedded(); return fe ? E.appsRead(rom, F.feldApps(rom, fe.stub).apps) : []; }
/* Which T7Emu the save buttons are for: -1 the main one, otherwise the further one with this index */
/* 1.6.8: every program with a save, the main one ('m') and the ones in the menu (index in romExtras) */
function savePrograms() {
  const rom = state.ctx.rom, out = [];
  if (emuInstalled()) { const ty = F.emuType(rom); out.push({ key: 'm', kind: ty, name: mainName(rom) || 'EMU', saveOff: 0xC0000, off: 0xA0000 }); }
  romExtras(rom).forEach((x, i) => out.push({ key: i, kind: E.extraKind(x), name: x.name, saveOff: x.saveOff, off: x.off }));
  return out;
}
function saveFor() { const v = $('saveFor').value; return v === 'm' || v === '' || v === undefined ? -1 : +v; }
function saveProg() { const v = $('saveFor').value; return savePrograms().find((p) => String(p.key) === v) || null; }
function renderSave() {
  if (!state.ctx) return;
  const ps = savePrograms(), sel = $('saveFor'), cur = sel.value;
  sel.innerHTML = '';
  for (const p of ps) { const o = document.createElement('option'); o.value = String(p.key); o.textContent = p.name + (p.key === 'm' ? ' (' + t('progIsMain') + ')' : ''); sel.appendChild(o); }
  if ([...sel.options].some((o) => o.value === cur)) sel.value = cur;
  $('saveForRow').hidden = !ps.length;
  const p = saveProg(), f = !p ? '.sav / .bin' : p.kind === 'tama' ? '.sav' : '.bin';
  $('btnEmuOut').textContent = t('saveBtnOut', { f }); $('btnEmuIn').textContent = t('saveBtnIn', { f });
  $('btnEmuOut').disabled = !p || p.kind === 't18';
  $('btnEmuIn').disabled = !!p && p.kind === 't18';
  $('btnEmuOut').title = p && p.kind === 't18' ? t('saveT18No') : '';
  $('saveStat').textContent = !p ? t('saveNoProg') : p.kind === 't18' ? t('saveT18No') : t('saveProgInfo');
  let con = false; try { con = !!dv.d && !dv.run; } catch (e) {}
  $('btnParaIn').disabled = !con;
  // 1.6.8: which save (the menu keeps several)
  const ss = paraSlots(), sl = $('paraSlot'), sc = sl.value;
  sl.innerHTML = '';
  $('paraSlotRow').hidden = !ss;
  if (ss) {
    const add = (v, txt) => { const o = document.createElement('option'); o.value = v; o.textContent = txt; sl.appendChild(o); };
    if (ss.live < 0) add('live', t('slotLive', { n: F.fieldNow(state.ctx.rom) }));
    for (const x of ss.list) add(x.i === ss.live ? 'live' : String(x.i), x.i === ss.live ? t('slotLive', { n: slotLabel(ss, x.i) }) : x.used ? slotLabel(ss, x.i) : t('slotEmpty', { n: slotLabel(ss, x.i) }));
    if ([...sl.options].some((o) => o.value === sc)) sl.value = sc;
  }
}
function paraSlots() { const fe = F.embedded(); try { return state.ctx && fe ? F.saveSlots(state.ctx.rom, fe.stub) : null; } catch (e) { return null; } }
function slotLabel(ss, i) { return ss.saves ? t('saveSlot', { n: i + 1 }) : (F.fieldNames(state.ctx.rom)[i] || t('saveSlot', { n: i + 1 })); }
function paraSlotPick() { return $('paraSlotRow').hidden ? 'live' : $('paraSlot').value; }
function t7SaveOutSel() {
  const p = saveProg(); if (!p || p.kind !== 't7') return null;
  return p.key === 'm' ? SV.t7SaveOut(state.ctx.rom) : SV.t7SaveOut(state.ctx.rom, p.saveOff);
}
$('saveFor').onchange = () => renderSave();
let g_slotIn = 'live';
$('btnParaIn').onclick = () => { g_slotIn = paraSlotPick(); $('fileSave').value = ''; $('fileSave').click(); };
$('btnEmuIn').onclick = () => $('fileEmuSav').click();
$('fileEmuSav').onchange = async (e) => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  const g = new Uint8Array(await f.arrayBuffer()), p = saveProg();
  if (SV.t7SaveCheck(g)) {
    if (p && p.kind === 't7' && p.key !== 'm') { state.t7saveX = { i: p.key, save: g }; renderPlan(); $('saveStat').textContent = t('t7SaveXDone', { n: p.name }); return; }
    if (!romT7(state.ctx.rom)) { toast(t('t7SaveNeed'), true); return; }
    state.t7save = g; renderPlan();
    $('saveStat').textContent = t('t7SaveInDone');
    return;
  }
  // V1Emu: session and settings (8 KB, "V1ST" at the start or an empty session)
  if (g.length === 0x2000 && p && p.kind === 'v1') {
    state.saveIn = { key: p.key, kind: 'v1', data: g }; renderPlan();
    $('saveStat').textContent = t('t7SaveXDone', { n: p.name }); return;
  }
  let dev, pk;
  try { dev = E.emuDetect(g); pk = E.packGuest(g, dev); } catch (err) { toast(t('saveEmuBad'), true); return; }
  // a colour Tama in the menu: its firmware with the save replaces the one in the ROM
  if (p && p.kind === 'tama' && p.key !== 'm') {
    const x = (state.extras || [])[p.key];
    if (!x || x.kind !== 'tama' || x.dev !== dev) { toast(t('saveEmuBad'), true); return; }
    x.guest = g; state.extrasDirty = true; state.saveIn = { key: p.key, kind: 'tama' }; renderEmu();
    $('saveStat').textContent = t('t7SaveXDone', { n: p.name }); return;
  }
  if (!appRoom()) { toast(t('saveNoRoom'), true); return; }
  const cfg = romEmuCfg(state.ctx.rom), was = state.emu ? state.emu.dev : cfg && cfg.dev;
  if (was && was !== dev && !confirm(t('saveEmuOther', { a: E.DEVICES[was].label, b: E.DEVICES[dev].label }))) return;
  state.emu = { guest: g, dev, size: pk.blob.length, big: pk.big }; emuRemoveClear(); if (!state.launch) state.doom = null;
  state.saveIn = { key: 'm', kind: 'tama' };
  renderDoom(); renderEmu();
  $('saveStat').textContent = t('saveEmuInDone', { d: E.DEVICES[dev].label });
};
/* Change a Paradise save (1.6.8): level, character, care. Names come from the loaded ROM. */
let petSave = null, petName = '';
$('btnPetOpen').onclick = () => { $('filePet').value = ''; $('filePet').click(); };
$('filePet').onchange = async (e) => {
  const f = e.target.files[0]; if (!f || !state.ctx) return;
  let save; try { save = SV.paraDecode(new Uint8Array(await f.arrayBuffer())); } catch (err) { toast(em(err), true); return; }
  petOpen(save, f.name, false);
};
/* 1.6.8: the save of the loaded ROM itself; the change goes back into the ROM */
$('btnPetRom').onclick = () => {
  if (!state.ctx) return;
  const ed = state.ctx.editionId;
  petOpen({ stamp: 0, counter: 0, alarm: 0, status: 0, regs: [], pages: SV.paraFromImage(state.ctx.rom, ed), edition: ed }, '', true);
};
$('btnPetClose').onclick = () => { petSave = null; $('petRow').hidden = true; $('petStat').textContent = ''; };
let petFromRom = false;
function petOpen(save, name, fromRom) {
  const ed = save.edition || state.ctx.editionId;
  if (SV.family(ed) !== SV.family(state.ctx.editionId)) { toast(t('petOther'), true); return; }
  const pet = SV.paraPet(save, ed);
  if (!pet) { toast(t('petNoSet'), true); return; }
  petSave = save; petSave.edition = ed; petName = name; petFromRom = fromRom;
  const lv = $('petLevel'); lv.innerHTML = '';
  for (let i = 0; i <= 10; i++) { const o = document.createElement('option'); o.value = i; o.textContent = i; if (i === pet.level) o.selected = true; lv.appendChild(o); }
  const sel = $('petChar'); sel.innerHTML = '';
  const keep = document.createElement('option'); keep.value = ''; keep.textContent = t('petKeep'); sel.appendChild(keep);
  const chars = F.characters(state.ctx.rom), fn = F.fieldNames(state.ctx.rom);
  let cur = '';
  for (const [st, k] of [[3, 'petKid'], [4, 'petYoung'], [5, 'petAdult']]) {
    const g = document.createElement('optgroup'); g.label = t(k);
    for (const c of chars.filter((x) => x.stage === st)) {
      const o = document.createElement('option'); o.value = c.id + ':' + c.stage;
      const nm = c.name.charAt(0) + c.name.slice(1).toLowerCase();
      o.textContent = nm + (c.field >= 0 && fn[c.field] ? ' (' + fn[c.field] + ')' : '');
      if (c.id === pet.id) cur = nm;
      g.appendChild(o);
    }
    sel.appendChild(g);
  }
  $('petCare').checked = false;
  $('petRow').hidden = false;
  $('petStat').textContent = t('petNow', { l: pet.level, c: cur || pet.id, h: pet.hunger, j: pet.happy, s: pet.sick ? t('petSick') : '' });
}
$('btnPetSave').onclick = () => {
  if (!petSave) return;
  const ed = petSave.edition, pet = SV.paraPet(petSave, ed), ch = {};
  const lv = +$('petLevel').value; if (lv !== pet.level) ch.level = lv;
  const v = $('petChar').value; if (v) { const [id, st] = v.split(':').map(Number); if (id !== pet.id || st !== pet.stage) { ch.id = id; ch.stage = st; } }
  if ($('petCare').checked) ch.care = true;
  if (!Object.keys(ch).length) { toast(t('petNothing')); return; }
  if (petFromRom) {
    const nw = SV.paraPetSet(petSave, ed, ch), r = new Uint8Array(state.ctx.rom);
    for (const [a, d] of nw.pages) if (d.length === 0x1000) r.set(d, a);
    state.ctx.rom = r; state.petDirty = true; petSave = nw; petSave.edition = ed;
    $('petStat').textContent = t('petRomDone'); renderPlan(); return;
  }
  const out = SV.paraWrite(SV.paraPetSet(petSave, ed, ch), ed);
  const name = petName.replace(/\.tamasave$/i, '') + '-' + stamp() + '.tamasave';
  download(out, name);
  $('petStat').textContent = t('petDone', { n: name });
};

$('btnParaOut').onclick = () => busy(async () => {
  const ed = state.ctx.editionId, v = paraSlotPick(), rom = state.ctx.rom;
  let pages = SV.paraFromImage(rom, ed), tag = '';
  if (v !== 'live') {   // 1.6.8: a save the menu keeps in a slot
    const ss = paraSlots(), i = +v, sv = ss && await F.slotReadAsync((a, n) => rom.subarray(a, a + n), ss.f, i);
    if (!sv) { toast(t('slotNone'), true); return; }
    const [a, b] = SV.region(ed), got = F.slotPages(sv);
    pages = new Map(); for (let p = a; p < b; p += 0x1000) pages.set(p, got.get(p) || new Uint8Array(0x1000).fill(0xFF));
    tag = '-' + slotLabel(ss, i).toLowerCase().replace(/[^a-z0-9]+/g, '');
  }
  const name = edSlug(ed) + tag + '-' + stamp() + '.tamasave';
  download(SV.paraEncode(pages, ed, null), name);
  $('saveStat').textContent = t('saveNoClock', { n: name });
});
$('btnEmuOut').onclick = () => busy(async () => {
  const p = saveProg(); if (!p) { toast(t('saveNoEmu'), true); return; }
  const rom = state.ctx.rom, slug = p.name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  if (p.kind === 't7') {
    const sv = t7SaveOutSel(); if (!sv) { toast(t('t7NoSave'), true); return; }
    const name = 't7emu-' + slug + '-' + stamp() + '-save.bin'; download(sv, name);
    $('saveStat').textContent = t('saveDone', { n: name }); return;
  }
  if (p.kind === 'v1') {
    const sv = rom.slice(p.saveOff, p.saveOff + 0x2000);
    if (sv.subarray(0, 0x1000).every((v) => v === 0xFF)) { toast(t('t7NoSave'), true); return; }
    const name = 'v1emu-' + slug + '-' + stamp() + '-save.bin'; download(sv, name);
    $('saveStat').textContent = t('saveDone', { n: name }); return;
  }
  if (p.kind === 'tama') {
    const cfg = p.key === 'm' ? romEmuCfg(rom) : SV.emuCfg(rom.subarray(p.off, p.off + 0x1000)); if (!cfg) { toast(t('saveNoEmu'), true); return; }
    const g = await SV.emuGuestAsync((a, n) => rom.subarray(a, a + n), cfg);
    const name = cfg.app + '-' + (p.key === 'm' ? '' : slug + '-') + stamp() + '.sav';
    download(g, name);
    $('saveStat').textContent = t('saveDone', { n: name }); return;
  }
  toast(t('saveT18No'), true);
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
$('btnSaveWrite').onclick = () => { g_slotIn = 'live'; $('fileSave').value = ''; $('fileSave').click(); };
$('fileSave').onchange = async (e) => {
  const f = e.target.files[0]; if (!f || !dv.d) return;
  let save;
  try { save = SV.paraDecode(new Uint8Array(await f.arrayBuffer())); } catch (err) { toast(em(err), true); return; }
  const ed = dv.d.info.edition;
  const chk = SV.paraCheck(save, ed);
  chk.errors.forEach((x) => { if (x.p) { x.p.s = FL.EDITIONS[x.p.s] || x.p.s; x.p.d = FL.EDITIONS[x.p.d] || x.p.d; } });
  if (chk.errors.length) { devRender(t('devErr', { e: chk.errors.map(saveMsg).join(', ') })); return; }
  const warn = chk.warnings.map(saveMsg).join('\n');
  if (g_slotIn !== 'live') { slotWrite(save, chk, +g_slotIn, warn); return; }
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

/* 1.6.8: a save into a slot of the menu on the device (packed like the menu does it, header page last) */
function slotWrite(save, chk, i, warn) {
  const ss = paraSlots(), rom = state.ctx.rom, ed = dv.d.info.edition;
  if (!ss) return;
  if (!ss.saves) {
    const set = chk.pages.get(0xEFE000), fi = F.fieldOfSettings(rom, set);
    if (fi !== i) { devRender(t('devErr', { e: t('slotField', { a: F.fieldNames(rom)[fi] || '?', b: slotLabel(ss, i) }) })); return; }
  }
  if (!confirm(t('slotSure', { n: slotLabel(ss, i) }) + (warn ? '\n\n' + warn : ''))) return;
  devTask(async () => {
    const d = dv.d, f = ss.f, [a, b] = SV.region(ed);
    const mine = rom.subarray(f.off + 0x140, f.off + 0x140 + 0x180), dev = (await d.read(f.off, 0x1000)).subarray(0x140, 0x140 + 0x180);
    for (const o of [0, 8, 12, 16, 364, 368]) if (mine[o] !== dev[o] || mine[o + 1] !== dev[o + 1] || mine[o + 2] !== dev[o + 2] || mine[o + 3] !== dev[o + 3]) throw new Error(t('slotDevOther'));
    const heads = [];
    for (let j = 0; j < f.nslots; j++) heads.push(new Uint8Array(await d.read(F.slotAddr(f, j), 0x1000)));
    const s = F.slotBuild(chk.pages, i, a, b), img = new Uint8Array(0x1000000).fill(0xFF);
    const written = F.slotPlace(img, f, i, s, F.poolUsed(f, heads, i));
    const first = new Map([[written[0], new Uint8Array(0x1000).fill(0xFF)]]);
    for (const p of written.slice(1)) first.set(p, img.slice(p, p + 0x1000));
    let w = await d.writeSectors(first, devProg, () => dv.cancel);
    w += await d.writeSectors(new Map([[written[0], img.slice(written[0], written[0] + 0x1000)]]), devProg, () => dv.cancel);
    devRender(t('slotDone', { n: slotLabel(ss, i) }));
  });
}
window.addEventListener('beforeunload', (e) => { if (state.ctx && state.changed.size && state.saved !== state.changed.size) { e.preventDefault(); e.returnValue = t('leave'); } });

applyLang(); devRender();
window.__paramix = state; // for tests
})();
