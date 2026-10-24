import { Text, View } from "@pocketjs/framework/solid/components";

export default function App() {
  return (
    <View
      class="w-full h-full items-center justify-center"
      style={{ bgColor: "#FFFFFF" }}
    >
      <Text class="text-2xl font-bold" style={{ textColor: "#000000" }}>
        Hello World
      </Text>
    </View>
  );
}
