# GicaEngineV3

![Static Badge](https://img.shields.io/badge/Version-3.0-blue) ![Static Badge](https://img.shields.io/badge/License-GNU_General_Public_License_V3.0-green)




## Sta je GicaEngine V3?
GicaEngine V3 je game engine napisan u C++.

## Sadrzaj
- [Funkcije](#funkcije)
- [Kompajliranje (proces)](#kompajliranje-proces)
- [Demonstracija](#demonstracija)
- [GameMain.cpp i funkcije](#gamemaincpp-i-funkcije)
- [Kako kompajlirati?](#kako-kompajlirati)

## Funkcije

- [X] GUI sa ImGui
- [X] Ucitavanje .obj modela (i to sa sopstvenim parser-om)
- [X] Ucitavanje tekstura
- [X] UI elementi u igrici (za sad dugmici i tekst)
- [X] Kompajliranje igrice
- [X] Opcije da se projekat sacuva i pokrene
- [X] Svetla (za sad samo *pointlight*)
- [X] Fizika, koristeci PhysX
- [X] Vise kamera

## Kompajliranje (proces)
Tokom kompilacije, program ce automatski generisati GameMain.cpp kod koji ima sve funkcije potrebne za igricu.
Program ce takodje sve modele i teksture kompresovati u jedan fajl, main.rpf.
GameMain.cpp ce main.rpf automatski dekompresovati i drzati u memoriji, tako da izvlacenje modela i tekstura iz igrice skoro nemoguce (ukoliko ne znas strukturu fajla).
Main.rpf sadrzi XML fajl koji sadrzi sve informacije o sceni.
GameMain.cpp moze se izmeniti, ali glavni su **OnInit** i **OnUpdate** funkcije.
GameMain.cpp takodje sadrzi i neophodne funkcije za dobijanje modela, tekstura, svetala, UI elemenata, itd. kako bi ih menjali.
Krajnji projekat bice u "out" folderu, gde je potrebno pokrenuti `build.bat` fajl ponovo, i igrica ce biti u "build" folderu.
U demonstraciji moze se videti vise o tome.

## Demonstracija

### UI

<img width="1919" height="1079" alt="image" src="https://github.com/user-attachments/assets/ff479546-35e8-42e4-8e3f-1e3496ec753b" />


### Ucitavanje modela

![demo_1_4](https://github.com/user-attachments/assets/73111e55-f59d-4f6c-af0a-d254b5c7a858)


### Ucitavanje tekstura

![demo_2_4](https://github.com/user-attachments/assets/015cbd2e-ecf6-43be-9e8e-9c31e91914d5)


### Dodavanje svetla (za sada samo pointlight)

![demo_3_4](https://github.com/user-attachments/assets/9dc05ceb-bc8f-4f1a-9d02-2054c65fdd3f)


### Kamera (FOV)

![demo_4_4](https://github.com/user-attachments/assets/9c20d67a-a3f8-4d7a-b4dc-0786dca0b8d6)


### Pomeranje i rotiranje modela u sceni

![demo_5_4](https://github.com/user-attachments/assets/bdde5ebb-4a1d-404f-9b25-a8ba02c686d3)


### Svetla, pozicija i boja, intenzitet i precnik

![demo_6_4](https://github.com/user-attachments/assets/a8ff2fd6-ff71-4c21-b31a-e1063edfe8e4)


### Igrica (da, cela igrica)

![demo_7_4](https://github.com/user-attachments/assets/85c48208-f806-471c-82a7-bb9e342dcefa)

GameMain.cpp sam programirao za kontrolisanje bureta i padanje kutije, sve u OnUpdate() funkciji.
*Naravno, fizika ovde nije ukljucena*

### Fizika i programiranje koda pomocu ugradjenih funkcija

![demo_8_4](https://github.com/user-attachments/assets/b0a15194-4a5e-4408-a5bf-fe78ed849c82)

Ovde sam takodje isprogramirao svetlo u OnUpdate() funkciji.

### I tako dalje (mrzi me dalje da snimam :P)

## GameMain.cpp i funkcije

### Glavne funkcije

**OnUpdate()**:
Ovde stoji kod koji ce se pokrenuti svaki frame.

**OnInit()**:
Ovde stoji kod koji ce se pokrenuti na pocetku igrice.

**"API"**:
Ne postoji pravi API, ali postoje funkcije u samom kodu:

Za dobijanje modela:

```cpp
int getModel(std::string name)
{
    for(int i = 0; i < g_models.size(); ++i)
    {
        if(g_models[i].name == name)
            return i;
    }
    return -1;
}
```

Primer (catcher igrica):

```cpp
    int boxIndex = getModel("Box");
    if(score < 7)
        g_models[boxIndex].position.y -= 0.01f;
```

U primeru kako bi promenili poziciju, rotaciju, teksturu, itd. nekog modela, potreban je njegov index. A index se dobije pomocu funkcije getModel.
Sve sto treba da znate jeste ime modela, u mom slucaju "Box". Nakon toga mozete menjati svojstva modela pomocu `g_models[index]`.

Slicno je i za dobijanje UI elemenata:

```cpp
int getUIElement(std::string name)
{
    for(int i = 0; i < uiElements.size(); ++i)
    {
        if(uiElements[i]->name == name)
            return i;
    }
    return -1;
}
```

Primer (catcher igrica):

```cpp
    int scoreLabel = getUIElement("ScoreLabel");
    std::string scoreText = "Score: " + std::to_string(score);
    static_cast<UILabel*>(uiElements[scoreLabel].get())->text = scoreText; 
```

U primeru se promeni tekst UI elementa "ScoreLabel".

Isto radi, isti je princip kao i `getModel()` funkcija.

Za pointlight:

```cpp
int getPointLight(std::string name)
{
    for(int i = 0; i < plNames.size(); ++i)
    {
        if(plNames[i] == name)
            return i;
    }
    return -1;
}
```

Primer (kao i u primeru sa kutijom):

```cpp
    int box_light_i = getPointLight("Point Light");
    static int dir = -1;
    float limit = 2.0f;
    float step = 0.001f;

    pointLights[box_light_i].position.x += step * dir;

    if(pointLights[box_light_i].position.x <= -limit || pointLights[box_light_i].position.x >= limit)
        dir *= -1;
```

Ovaj kod svaki frame (OnUpdate) menja poziciju svetla "Point Light" za `step`.

Isto radi, isti je princip.

## Kako kompajlirati?

Prosto pokreni `build_msvc.bat` fajl i sve ce se odraditi samo. Nastace novi folder, "build". Unutar njega, "Release", i tu ce biti GameMain.exe i main.rpf. Takodje, potrebne su sledece biblioteke kompajlirane samostalno uz pomoc Visual Studia:

- [GLFW](https://github.com/glfw/glfw)
- [glm](https://github.com/g-truc/glm)
- [DirectXTex](https://github.com/microsoft/DirectXTex)
- [DirectXTK](https://github.com/microsoft/DirectXTK)
- [ImGui](https://github.com/ocornut/imgui)
- [ZLib](https://zlib.net/)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
- [tinyxml2](https://github.com/leethomason/tinyxml2)
- [PhysX](https://github.com/NVIDIA-Omniverse/PhysX) (Release x64)

*Takodje imate i kompajliran program u [Releases](https://github.com/NotStrahinja/GicaEngineV3/releases/tag/V3.0)*
