from playwright.sync_api import sync_playwright
import subprocess
import socket
import time


CHROME_PATH = (
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
)

CHROME_PROFILE = (
    r"C:\Users\Acey\Downloads\Dove Agent Build"
    r"\Agent Modules\Chrome_Agent"
)

DEBUGGING_HOST = "***"
DEBUGGING_PORT = ***
DEBUGGING_URL = f"http://{DEBUGGING_HOST}:{DEBUGGING_PORT}"


def debugging_port_is_open():
    try:
        with socket.create_connection(
            (DEBUGGING_HOST, DEBUGGING_PORT),
            timeout=1
        ):
            return True

    except OSError:
        return False


def start_browser():

    if debugging_port_is_open():
        print("Agent browser already running.")
        return

    subprocess.Popen(
        [
            CHROME_PATH,

            f"--remote-debugging-address={DEBUGGING_HOST}",
            f"--remote-debugging-port={DEBUGGING_PORT}",

            f"--user-data-dir={CHROME_PROFILE}",

            "--no-first-run",
            "--no-default-browser-check",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    deadline = time.time() + 20

    while time.time() < deadline:

        if debugging_port_is_open():
            print("Agent browser ready.")
            return

        time.sleep(0.25)

    raise RuntimeError(
        f"Chrome did not open debugging port {DEBUGGING_PORT}."
    )


def findAnimations(context):

    page = next(
        (
            current_page
            for current_page in context.pages
            if "mixamo.com" in current_page.url
        ),
        None
    )

    if page is None:
        page = context.new_page()

    page.goto(
        "https://www.mixamo.com/#/",
        wait_until="domcontentloaded",
        timeout=60_000
    )
    
    page.get_by_role("link", name="Animations").click()

    page.bring_to_front()
    page.pause()


if __name__ == "__main__":

    start_browser()

    with sync_playwright() as p:
        browser = p.chromium.connect_over_cdp(
            DEBUGGING_URL
        )

        if not browser.contexts:
            raise RuntimeError(
                "Chrome connected, but no browser context was found."
            )


        context = browser.contexts[0]

        findAnimations(context)
