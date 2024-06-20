# RC Car Controller by nRF52840

## 소스코드 리스트

### main.c
- **기능**: 컨트롤러 구동에 필요한 모든 함수(블루투스 스캔, 조이스틱 입력 등)가 호출되어서 컨트롤러의 기능들이 실질적으로 실행되는 파일
- **추가 설명**: multithreading 또한 이 파일에서 구현

### gpios.c
- **기능**: GPIO핀을 사용한 버튼 및 LED의 configuration 함수 정의
- **추가 설명**: 인터럽트용 callback 함수들 또한 여기에 정의됨

### led.c
- **기능**: RICH SHIELD에 장착된 LED matrix에 조이스틱의 입력 방향을 화살표로 표시해주는 함수 정의

### my_lbs.c
- **기능**: 조이스틱을 통해 입력받은 방향에 해당하는 알파벳을 다른 마이크로프로세서에게 블루투스를 통해 전송할 때 필요한 함수 정의

## 빌드하는 방법
VSCode의 nRF Connect Extension을 사용하여 손쉽게 빌드할 수 있고, 보드에 flash까지 가능하다.
___
**```View``` -> ```Extensions``` -> ```"nrf connect" 검색```-> ```"nRF Connect for VS Code Extension Pack" 설치```**  <br><br>
위 과정을 통해 VSCode에서 nRF Connect를 설치하고 사용할 수 있다.
___
```sh
git clone https://github.com/Samuel0004/controller
```
**```Open an existing application``` -> 프로젝트 선택**<br><br>
프로젝트 clone 후, 이를 nRF Connect를 통해 실행한다.
___
**```Add build configuration``` -> Configuration: ```prj.conf```선택, Add overlay: ```nrf52840dk_nrf52840.overlay```선택**
<br><br>
빌드를 위해 빌드 설정이 필요하다. 이때, ```prj.conf```와 ```nrf52840dk_nrf52840.overlay``` 파일을 사용하여 빌드 설정을 해야 한다.
___
이러한 과정을 통해 build configuration이 완료되었고, build 또는 보드에 flash가 가능하다.

## 프로젝트에 사용된 오픈소스 정보

### my_lbs.c
- **출처**: Nordic 사에서 제공한 예제 코드
- **설명**: LED Button Service를 구현하는 함수들이 존재한다. 이 코드를 수정하여 notification으로 숫자를 보내는 것이 아닌, 알파벳을 보낼 수 있게 하였다.

### led.c
- **출처**: 수업 시간에 제공된 Github에 올라와 있는 예제 중, 조이스틱 예제(T5_2)
- **설명**: 조이스틱의 방향을 LED matrix에 표시할 수 있다.

****
**Copyright 2024. (김성민, 박재언) All rights reserved.**

- **소스 작성자**: 김성민, 박재언
- **최종 수정 날짜**: 2024년 6월 20일

